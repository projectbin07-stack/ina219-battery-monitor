## What this changes

## Why

## How it was checked

- [ ] `pio test -e native`
- [ ] `pio run` for the board environments affected
- [ ] `ruff check tools && ruff format --check tools && mypy && pytest` (if `tools/` changed)
- [ ] Tried on real hardware (say which board and sensor, or "not tested on hardware")

## Firmware size (if firmware code changed)

Paste the RAM / Flash lines PlatformIO prints for `nanoatmega328`.
