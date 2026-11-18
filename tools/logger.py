#!/usr/bin/env python3
"""Host-side logger for ina219-battery-monitor telemetry.

The firmware prints one frame per sample on its serial port::

    $BM1,<seq>,<t_ms>,<bus_mV>,<current_uA>,<power_uW>,<charge_uAh>,<energy_uWh>,<soc_permille>,<flags>*<CRC8>

All fields are decimal integers. The checksum is CRC-8 (polynomial 0x07,
initial value 0x00, no reflection, no final XOR) over every character between
``$`` and ``*`` (both excluded), written as two upper-case hex digits. Lines
starting with ``#`` are human-readable status messages from the firmware.

Sub-commands:

``record``  read frames from a serial port, validate them and write a CSV file
            (needs ``pyserial``); optionally keep the raw lines too.
``replay``  re-decode a raw log captured earlier and write the same CSV, or
            play it back to stdout at the original pace.

Only the standard library is needed for ``replay`` and for importing this
module; ``pyserial`` is imported lazily by ``record``.
"""

from __future__ import annotations

import argparse
import contextlib
import csv
import sys
import time
from collections.abc import Callable, Iterable, Iterator
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import TextIO

FRAME_TAG = "BM1"
FIELD_COUNT = 9

FLAG_OVERFLOW = 0x01  # INA219 math overflow (OVF) was set for this sample
FLAG_NOT_READY = 0x02  # conversion-ready (CNVR) was not set; values are stale
FLAG_SENSOR_ERROR = 0x04  # I2C transaction failed; measurement fields are zero
FLAG_CUTOFF = 0x08  # low-voltage cutoff is tripped

FLAG_NAMES: dict[int, str] = {
    FLAG_OVERFLOW: "overflow",
    FLAG_NOT_READY: "not-ready",
    FLAG_SENSOR_ERROR: "sensor-error",
    FLAG_CUTOFF: "cutoff",
}

UINT32_MAX = 0xFFFFFFFF
INT32_MIN = -(2**31)
INT32_MAX = 2**31 - 1

CSV_HEADER = [
    "host_time",
    "seq",
    "t_ms",
    "bus_v",
    "current_ma",
    "power_mw",
    "charge_mah",
    "energy_mwh",
    "soc_pct",
    "flags",
]


class FrameError(ValueError):
    """Raised when a line is not a valid telemetry frame."""


def crc8(data: bytes, crc: int = 0x00) -> int:
    """CRC-8 with polynomial x^8 + x^2 + x + 1 (0x07), MSB first, no reflection."""
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


@dataclass(frozen=True)
class Frame:
    """One decoded telemetry frame, in the integer units used on the wire."""

    seq: int
    t_ms: int
    bus_mv: int
    current_ua: int
    power_uw: int
    charge_uah: int
    energy_uwh: int
    soc_permille: int
    flags: int

    @property
    def bus_v(self) -> float:
        return self.bus_mv / 1000.0

    @property
    def current_ma(self) -> float:
        return self.current_ua / 1000.0

    @property
    def power_mw(self) -> float:
        return self.power_uw / 1000.0

    @property
    def charge_mah(self) -> float:
        return self.charge_uah / 1000.0

    @property
    def energy_mwh(self) -> float:
        return self.energy_uwh / 1000.0

    @property
    def soc_pct(self) -> float:
        return self.soc_permille / 10.0

    def flag_names(self) -> list[str]:
        return [name for bit, name in FLAG_NAMES.items() if self.flags & bit]

    def encode(self) -> str:
        """Encode to the wire format (without a line terminator)."""
        body = (
            f"{FRAME_TAG},{self.seq},{self.t_ms},{self.bus_mv},{self.current_ua},"
            f"{self.power_uw},{self.charge_uah},{self.energy_uwh},"
            f"{self.soc_permille},{self.flags}"
        )
        return f"${body}*{crc8(body.encode('ascii')):02X}"

    def csv_row(self, host_time: str = "") -> list[str]:
        return [
            host_time,
            str(self.seq),
            str(self.t_ms),
            f"{self.bus_v:.3f}",
            f"{self.current_ma:.3f}",
            f"{self.power_mw:.3f}",
            f"{self.charge_mah:.3f}",
            f"{self.energy_mwh:.3f}",
            f"{self.soc_pct:.1f}",
            str(self.flags),
        ]


def _parse_int(text: str, low: int, high: int, name: str) -> int:
    digits = text[1:] if text.startswith("-") else text
    # str.isdigit() accepts some non-ASCII digits; the wire format is ASCII only.
    if not digits or not digits.isascii() or not digits.isdigit() or len(digits) > 10:
        raise FrameError(f"field {name!r} is not a decimal integer: {text!r}")
    value = int(text)
    if not low <= value <= high:
        raise FrameError(f"field {name!r} out of range: {value}")
    return value


def decode_frame(line: str) -> Frame:
    """Decode one line. Raises :class:`FrameError` if it is not a valid frame."""
    text = line.rstrip("\r\n")
    if not text.startswith("$"):
        raise FrameError("missing '$' start marker")
    body, star, checksum = text[1:].rpartition("*")
    if not star:
        raise FrameError("missing '*' checksum marker")
    if len(checksum) != 2 or any(c not in "0123456789ABCDEFabcdef" for c in checksum):
        raise FrameError(f"malformed checksum: {checksum!r}")
    try:
        raw = body.encode("ascii")
    except UnicodeEncodeError as exc:
        raise FrameError("non-ASCII character in frame") from exc
    expected = crc8(raw)
    if int(checksum, 16) != expected:
        raise FrameError(f"checksum mismatch: got {checksum.upper()}, computed {expected:02X}")
    fields = body.split(",")
    if fields[0] != FRAME_TAG:
        raise FrameError(f"unknown frame tag: {fields[0]!r}")
    if len(fields) != FIELD_COUNT + 1:
        raise FrameError(f"expected {FIELD_COUNT} fields, got {len(fields) - 1}")
    return Frame(
        seq=_parse_int(fields[1], 0, UINT32_MAX, "seq"),
        t_ms=_parse_int(fields[2], 0, UINT32_MAX, "t_ms"),
        bus_mv=_parse_int(fields[3], INT32_MIN, INT32_MAX, "bus_mV"),
        current_ua=_parse_int(fields[4], INT32_MIN, INT32_MAX, "current_uA"),
        power_uw=_parse_int(fields[5], INT32_MIN, INT32_MAX, "power_uW"),
        charge_uah=_parse_int(fields[6], INT32_MIN, INT32_MAX, "charge_uAh"),
        energy_uwh=_parse_int(fields[7], INT32_MIN, INT32_MAX, "energy_uWh"),
        soc_permille=_parse_int(fields[8], 0, 1000, "soc_permille"),
        flags=_parse_int(fields[9], 0, 255, "flags"),
    )


@dataclass
class LogStats:
    """Counters accumulated while processing a stream of lines."""

    frames: int = 0
    bad_lines: int = 0
    comments: int = 0
    dropped: int = 0  # frames missing according to the sequence counter
    restarts: int = 0  # sequence counter went backwards (device reset)
    last_seq: int | None = None

    def note_sequence(self, seq: int) -> None:
        if self.last_seq is not None:
            if seq > self.last_seq:
                self.dropped += seq - self.last_seq - 1
            else:
                self.restarts += 1
        self.last_seq = seq

    def summary(self) -> str:
        return (
            f"{self.frames} frames, {self.bad_lines} bad lines, {self.comments} comments, "
            f"{self.dropped} dropped, {self.restarts} restarts"
        )


def iter_frames(
    lines: Iterable[str],
    stats: LogStats,
    on_message: Callable[[str], None] | None = None,
) -> Iterator[Frame]:
    """Yield valid frames from ``lines``, updating ``stats`` for everything else.

    Blank lines are skipped, ``#`` lines are counted as comments, and anything
    that fails validation is counted in ``stats.bad_lines`` and reported through
    ``on_message``.
    """
    for line in lines:
        text = line.strip()
        if not text:
            continue
        if text.startswith("#"):
            stats.comments += 1
            if on_message is not None:
                on_message(text)
            continue
        try:
            frame = decode_frame(text)
        except FrameError as exc:
            stats.bad_lines += 1
            if on_message is not None:
                on_message(f"rejected line ({exc}): {text!r}")
            continue
        stats.frames += 1
        stats.note_sequence(frame.seq)
        yield frame


def write_csv(
    frames: Iterable[Frame],
    out: TextIO,
    host_clock: Callable[[], str] | None = None,
    flush: bool = False,
) -> int:
    """Write the CSV header and one row per frame. Returns the row count."""
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(CSV_HEADER)
    rows = 0
    for frame in frames:
        writer.writerow(frame.csv_row(host_clock() if host_clock is not None else ""))
        rows += 1
        if flush:
            out.flush()
    return rows


def format_frame(frame: Frame) -> str:
    """One-line human-readable rendering used by ``replay`` without ``--out``."""
    flags = ",".join(frame.flag_names()) or "-"
    return (
        f"{frame.t_ms / 1000.0:10.3f}s  {frame.bus_v:6.3f} V  {frame.current_ma:9.3f} mA  "
        f"{frame.power_mw:9.3f} mW  {frame.charge_mah:9.3f} mAh  {frame.soc_pct:5.1f} %  {flags}"
    )


def paced(frames: Iterable[Frame], speed: float, sleep: Callable[[float], None]) -> Iterator[Frame]:
    """Yield frames, sleeping between them according to their device timestamps."""
    previous: int | None = None
    for frame in frames:
        if previous is not None:
            delta_ms = (frame.t_ms - previous) & UINT32_MAX  # tolerates millis() wraparound
            if 0 < delta_ms < 60_000:
                sleep(delta_ms / 1000.0 / speed)
        previous = frame.t_ms
        yield frame


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def _stderr(message: str) -> None:
    print(message, file=sys.stderr)


def _serial_lines(
    port: str, baud: int, duration: float | None, raw: TextIO | None
) -> Iterator[str]:
    try:
        import serial  # type: ignore[import-untyped,import-not-found,unused-ignore]
    except ImportError as exc:  # pragma: no cover - depends on the environment
        raise SystemExit("pyserial is required for 'record': pip install pyserial") from exc

    deadline = None if duration is None else time.monotonic() + duration
    with serial.Serial(port, baud, timeout=1.0) as link:  # pragma: no cover - needs hardware
        while deadline is None or time.monotonic() < deadline:
            data = link.readline()
            if not data:
                continue
            line = data.decode("ascii", errors="replace")
            if raw is not None:
                raw.write(line.rstrip("\r\n") + "\n")
                raw.flush()
            yield line


def cmd_record(args: argparse.Namespace) -> int:  # pragma: no cover - needs hardware
    stats = LogStats()
    raw = open(args.raw, "w", encoding="ascii", errors="replace") if args.raw else None  # noqa: SIM115
    try:
        with open(args.out, "w", encoding="utf-8", newline="") as out:
            lines = _serial_lines(args.port, args.baud, args.duration, raw)
            frames = iter_frames(lines, stats, None if args.quiet else _stderr)
            with contextlib.suppress(KeyboardInterrupt):  # Ctrl+C ends the recording
                write_csv(frames, out, host_clock=_utc_now, flush=True)
    finally:
        if raw is not None:
            raw.close()
    _stderr(stats.summary())
    return 0


def cmd_replay(args: argparse.Namespace) -> int:
    stats = LogStats()
    with open(args.log, encoding="ascii", errors="replace") as source:
        frames = iter_frames(source, stats, None if args.quiet else _stderr)
        if args.realtime:
            frames = paced(frames, args.speed, time.sleep)
        if args.out:
            with open(args.out, "w", encoding="utf-8", newline="") as out:
                write_csv(frames, out)
        else:
            for frame in frames:
                print(format_frame(frame), flush=args.realtime)
    _stderr(stats.summary())
    return 0 if stats.frames > 0 else 1


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="logger.py", description="Log and replay ina219-battery-monitor telemetry frames."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    record = sub.add_parser("record", help="read frames from a serial port and write CSV")
    record.add_argument("--port", required=True, help="serial port, e.g. COM5 or /dev/ttyUSB0")
    record.add_argument("--baud", type=int, default=115200, help="baud rate (default: 115200)")
    record.add_argument("--out", required=True, help="CSV file to write")
    record.add_argument("--raw", help="also save the raw received lines here (input for 'replay')")
    record.add_argument("--duration", type=float, help="stop after this many seconds")
    record.add_argument("--quiet", action="store_true", help="do not echo comments and rejects")
    record.set_defaults(func=cmd_record)

    replay = sub.add_parser("replay", help="decode a raw log recorded earlier")
    replay.add_argument("log", help="raw log file (one received line per line)")
    replay.add_argument("--out", help="write CSV here instead of printing a table")
    replay.add_argument("--realtime", action="store_true", help="pace output by device timestamps")
    replay.add_argument("--speed", type=float, default=1.0, help="speed-up factor for --realtime")
    replay.add_argument("--quiet", action="store_true", help="do not echo comments and rejects")
    replay.set_defaults(func=cmd_replay)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if getattr(args, "speed", 1.0) <= 0:
        raise SystemExit("--speed must be positive")
    result: int = args.func(args)
    return result


if __name__ == "__main__":
    sys.exit(main())
