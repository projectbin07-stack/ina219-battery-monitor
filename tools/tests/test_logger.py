"""Tests for tools/logger.py.

The frame fixtures are the same strings asserted by the firmware encoder tests
in test/test_telemetry, so both ends of the link are checked against one set of
lines. ``data/sample_session.log`` is a hand-written log in the format the
firmware prints (it is not a capture from hardware).
"""

from __future__ import annotations

import csv
import io
from pathlib import Path

import pytest

import logger
from logger import Frame, FrameError, LogStats, crc8, decode_frame

DATA = Path(__file__).parent / "data"
SAMPLE_LOG = DATA / "sample_session.log"

TYPICAL = "$BM1,42,21000,3876,100000,386000,583,2261,512,0*D1"
TYPICAL_FRAME = Frame(42, 21000, 3876, 100000, 386000, 583, 2261, 512, 0)

SHARED_FIXTURES = [
    ("$BM1,0,0,0,0,0,0,0,0,0*78", Frame(0, 0, 0, 0, 0, 0, 0, 0, 0)),
    (TYPICAL, TYPICAL_FRAME),
    (
        "$BM1,43,21500,3872,-250300,-969000,548,2127,513,0*3F",
        Frame(43, 21500, 3872, -250300, -969000, 548, 2127, 513, 0),
    ),
    (
        "$BM1,4294967295,4294967295,2147483647,-2147483648,-2147483648,"
        "2147483647,-2147483648,1000,255*6F",
        Frame(
            4294967295,
            4294967295,
            2147483647,
            -2147483648,
            -2147483648,
            2147483647,
            -2147483648,
            1000,
            255,
        ),
    ),
    (
        "$BM1,44,22000,2990,1200000,3588000,715,2625,12,8*69",
        Frame(44, 22000, 2990, 1200000, 3588000, 715, 2625, 12, 8),
    ),
    ("$BM1,45,22500,0,0,0,715,2625,12,4*4E", Frame(45, 22500, 0, 0, 0, 715, 2625, 12, 4)),
]


def sealed(body: str) -> str:
    """Wrap a body in '$...*HH' with a correct checksum."""
    return f"${body}*{crc8(body.encode('ascii')):02X}"


# --- CRC ---------------------------------------------------------------------


def test_crc8_standard_check_value() -> None:
    assert crc8(b"123456789") == 0xF4


@pytest.mark.parametrize(
    ("data", "expected"),
    [
        (b"", 0x00),
        (b"\x00", 0x00),
        (b"\x01", 0x07),  # the polynomial itself
        (b"\x80", 0x89),  # shifts: 07 0E 1C 38 70 E0 C7 89
        (b"\xff", 0xF3),
        (b"A", 0xC0),
        (b"BM1", 0x75),
    ],
)
def test_crc8_vectors(data: bytes, expected: int) -> None:
    assert crc8(data) == expected


def test_crc8_can_be_chained() -> None:
    assert crc8(b"56789", crc8(b"1234")) == 0xF4


# --- decoding ----------------------------------------------------------------


@pytest.mark.parametrize(("line", "frame"), SHARED_FIXTURES)
def test_decode_shared_fixtures(line: str, frame: Frame) -> None:
    assert decode_frame(line) == frame


@pytest.mark.parametrize(("line", "frame"), SHARED_FIXTURES)
def test_encode_shared_fixtures(line: str, frame: Frame) -> None:
    assert frame.encode() == line


@pytest.mark.parametrize("ending", ["", "\n", "\r\n"])
def test_decode_accepts_line_endings(ending: str) -> None:
    assert decode_frame(TYPICAL + ending) == TYPICAL_FRAME


def test_decode_accepts_lowercase_checksum() -> None:
    assert decode_frame(TYPICAL[:-2] + "d1") == TYPICAL_FRAME


def test_scaled_properties() -> None:
    frame = decode_frame("$BM1,43,21500,3872,-250300,-969000,548,2127,513,0*3F")
    assert frame.bus_v == pytest.approx(3.872)
    assert frame.current_ma == pytest.approx(-250.3)
    assert frame.power_mw == pytest.approx(-969.0)
    assert frame.charge_mah == pytest.approx(0.548)
    assert frame.energy_mwh == pytest.approx(2.127)
    assert frame.soc_pct == pytest.approx(51.3)


def test_flag_names() -> None:
    assert TYPICAL_FRAME.flag_names() == []
    assert Frame(0, 0, 0, 0, 0, 0, 0, 0, 8).flag_names() == ["cutoff"]
    assert Frame(0, 0, 0, 0, 0, 0, 0, 0, 0x0F).flag_names() == [
        "overflow",
        "not-ready",
        "sensor-error",
        "cutoff",
    ]


@pytest.mark.parametrize(
    ("line", "message"),
    [
        ("", "start marker"),
        ("#OK RESET", "start marker"),
        (TYPICAL[1:], "start marker"),
        (TYPICAL[:-3], "checksum marker"),
        (TYPICAL[:-1], "malformed checksum"),
        (TYPICAL[:-2] + "G1", "malformed checksum"),
        (TYPICAL + "junk", "malformed checksum"),
        (TYPICAL[:-2] + "D2", "checksum mismatch"),
        (TYPICAL.replace("3876", "3877"), "checksum mismatch"),
        (TYPICAL.replace("3876", "3786"), "checksum mismatch"),
        ("$BM1,42,21000,3876*D1", "checksum mismatch"),
    ],
)
def test_decode_rejects_framing_and_corruption(line: str, message: str) -> None:
    with pytest.raises(FrameError, match=message):
        decode_frame(line)


@pytest.mark.parametrize(
    ("body", "message"),
    [
        ("BM2,0,0,0,0,0,0,0,0,0", "unknown frame tag"),
        ("", "unknown frame tag"),
        ("BM1", "expected 9 fields, got 0"),
        ("BM1,1,2,3", "expected 9 fields, got 3"),
        ("BM1,0,0,0,0,0,0,0,0", "expected 9 fields, got 8"),
        ("BM1,0,0,0,0,0,0,0,0,0,0", "expected 9 fields, got 10"),
        ("BM1,x,0,0,0,0,0,0,0,0", "not a decimal integer"),
        ("BM1,1,,0,0,0,0,0,0,0", "not a decimal integer"),
        ("BM1,0,0,3.8,0,0,0,0,0,0", "not a decimal integer"),
        ("BM1,0,0,-,0,0,0,0,0,0", "not a decimal integer"),
        ("BM1,0,0,+5,0,0,0,0,0,0", "not a decimal integer"),
        ("BM1,0,0, 5,0,0,0,0,0,0", "not a decimal integer"),
        ("BM1,12345678901,0,0,0,0,0,0,0,0", "not a decimal integer"),
        ("BM1,-1,0,0,0,0,0,0,0,0", "'seq' out of range"),
        ("BM1,4294967296,0,0,0,0,0,0,0,0", "'seq' out of range"),
        ("BM1,0,0,2147483648,0,0,0,0,0,0", "'bus_mV' out of range"),
        ("BM1,0,0,0,-2147483649,0,0,0,0,0", "'current_uA' out of range"),
        ("BM1,0,0,0,0,0,0,0,1001,0", "'soc_permille' out of range"),
        ("BM1,0,0,0,0,0,0,0,0,256", "'flags' out of range"),
    ],
)
def test_decode_rejects_bad_content_with_valid_crc(body: str, message: str) -> None:
    with pytest.raises(FrameError, match=message):
        decode_frame(sealed(body))


def test_every_single_bit_flip_is_detected() -> None:
    body_end = len(TYPICAL) - 3
    for index in range(1, body_end):
        for bit in range(7):
            corrupted = (
                TYPICAL[:index] + chr(ord(TYPICAL[index]) ^ (1 << bit)) + TYPICAL[index + 1 :]
            )
            with pytest.raises(FrameError):
                decode_frame(corrupted)


def test_non_ascii_is_rejected() -> None:
    with pytest.raises(FrameError, match="non-ASCII"):
        decode_frame("$BM1,٤٢,0,0,0,0,0,0,0,0*00")


# --- stream processing ---------------------------------------------------------


def test_sample_session_statistics() -> None:
    stats = LogStats()
    messages: list[str] = []
    with SAMPLE_LOG.open(encoding="ascii") as source:
        frames = list(logger.iter_frames(source, stats, messages.append))

    assert [f.seq for f in frames] == [0, 1, 2, 4, 0]
    assert stats.frames == 5
    assert stats.comments == 3
    assert stats.bad_lines == 2  # one truncated line, one with a wrong checksum
    assert stats.dropped == 1  # seq 3 never arrived
    assert stats.restarts == 1  # counter went 4 -> 0 after the second banner
    assert stats.summary() == "5 frames, 2 bad lines, 3 comments, 1 dropped, 1 restarts"

    assert messages[0] == "#ina219-battery-monitor 0.1.0"
    assert sum("checksum marker" in m for m in messages) == 1
    assert sum("checksum mismatch" in m for m in messages) == 1
    assert frames[0].flag_names() == ["not-ready"]
    assert frames[1].current_ma == pytest.approx(151.9)


def test_sequence_tracking() -> None:
    stats = LogStats()
    for seq in (10, 11, 15, 16, 2, 3):
        stats.note_sequence(seq)
    assert stats.dropped == 3  # 12, 13, 14
    assert stats.restarts == 1
    assert stats.last_seq == 3


def test_repeated_sequence_number_counts_as_restart() -> None:
    stats = LogStats()
    stats.note_sequence(0)
    stats.note_sequence(0)
    assert stats.restarts == 1
    assert stats.dropped == 0


def test_write_csv() -> None:
    out = io.StringIO()
    frames = [frame for _, frame in SHARED_FIXTURES[1:3]]
    rows = logger.write_csv(frames, out, host_clock=lambda: "2026-01-02T03:04:05.000+00:00")
    assert rows == 2
    parsed = list(csv.reader(io.StringIO(out.getvalue())))
    assert parsed[0] == logger.CSV_HEADER
    assert parsed[1] == [
        "2026-01-02T03:04:05.000+00:00",
        "42",
        "21000",
        "3.876",
        "100.000",
        "386.000",
        "0.583",
        "2.261",
        "51.2",
        "0",
    ]
    assert parsed[2][3:6] == ["3.872", "-250.300", "-969.000"]


def test_write_csv_without_clock_leaves_host_time_empty() -> None:
    out = io.StringIO()
    logger.write_csv([TYPICAL_FRAME], out)
    assert out.getvalue().splitlines()[1].startswith(",42,21000,")


def test_format_frame() -> None:
    text = logger.format_frame(Frame(44, 22000, 2990, 1200000, 3588000, 715, 2625, 12, 8))
    assert "22.000s" in text
    assert "2.990 V" in text
    assert "1200.000 mA" in text
    assert "1.2 %" in text
    assert text.endswith("cutoff")
    assert logger.format_frame(TYPICAL_FRAME).endswith("-")


def test_paced_sleeps_by_device_timestamps() -> None:
    frames = [
        Frame(0, 1000, 0, 0, 0, 0, 0, 0, 0),
        Frame(1, 1500, 0, 0, 0, 0, 0, 0, 0),
        Frame(2, 2500, 0, 0, 0, 0, 0, 0, 0),
    ]
    sleeps: list[float] = []
    assert list(logger.paced(frames, 2.0, sleeps.append)) == frames
    assert sleeps == pytest.approx([0.25, 0.5])


def test_paced_handles_wraparound_and_skips_long_gaps() -> None:
    frames = [
        Frame(0, 0xFFFFFF06, 0, 0, 0, 0, 0, 0, 0),  # 250 ms before millis() wraps
        Frame(1, 250, 0, 0, 0, 0, 0, 0, 0),  # 500 ms later
        Frame(2, 3_600_000, 0, 0, 0, 0, 0, 0, 0),  # an hour later: not replayed in real time
        Frame(3, 100, 0, 0, 0, 0, 0, 0, 0),  # device restarted
    ]
    sleeps: list[float] = []
    list(logger.paced(frames, 1.0, sleeps.append))
    assert sleeps == pytest.approx([0.5])


# --- command line --------------------------------------------------------------


def test_replay_to_csv(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    out = tmp_path / "session.csv"
    assert logger.main(["replay", str(SAMPLE_LOG), "--out", str(out)]) == 0
    with out.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.reader(handle))
    assert rows[0] == logger.CSV_HEADER
    assert len(rows) == 6
    assert rows[1] == ["", "0", "512", "4.012", "152.300", "611.000", "0.000", "0.000", "79.0", "2"]
    captured = capsys.readouterr()
    assert "5 frames, 2 bad lines, 3 comments, 1 dropped, 1 restarts" in captured.err
    assert "rejected line" in captured.err
    assert captured.out == ""


def test_replay_prints_table_and_quiet_suppresses_messages(
    capsys: pytest.CaptureFixture[str],
) -> None:
    assert logger.main(["replay", str(SAMPLE_LOG), "--quiet"]) == 0
    captured = capsys.readouterr()
    lines = captured.out.splitlines()
    assert len(lines) == 5
    assert "4.012 V" in lines[0]
    assert "152.300 mA" in lines[0]
    assert "rejected line" not in captured.err
    assert "5 frames" in captured.err


def test_replay_of_log_without_frames_fails(tmp_path: Path) -> None:
    empty = tmp_path / "empty.log"
    empty.write_text("#only a banner\nnot a frame\n", encoding="ascii")
    assert logger.main(["replay", str(empty), "--quiet"]) == 1


def test_replay_realtime_uses_speed(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    log = tmp_path / "two.log"
    log.write_text(
        "\n".join(
            [
                Frame(0, 1000, 3900, 0, 0, 0, 0, 500, 0).encode(),
                Frame(1, 2000, 3900, 0, 0, 0, 0, 500, 0).encode(),
            ]
        )
        + "\n",
        encoding="ascii",
    )
    sleeps: list[float] = []
    monkeypatch.setattr(logger.time, "sleep", sleeps.append)
    assert logger.main(["replay", str(log), "--realtime", "--speed", "10", "--quiet"]) == 0
    assert sleeps == pytest.approx([0.1])


def test_invalid_speed_is_rejected() -> None:
    with pytest.raises(SystemExit):
        logger.main(["replay", str(SAMPLE_LOG), "--speed", "0"])


def test_parser_requires_a_subcommand() -> None:
    with pytest.raises(SystemExit):
        logger.build_parser().parse_args([])
