# Contributing

Thanks for taking the time to look at this project. Bug reports, test results
from real hardware and small focused pull requests are all welcome.

## Before you open a pull request

1. **Unit tests** (no hardware needed):

   ```sh
   pio test -e native
   ```

2. **Firmware builds** for every board environment:

   ```sh
   pio run -e esp32dev
   pio run -e nanoatmega328
   pio run -e nanoatmega328_serial
   ```

   The Nano has 30 720 bytes of flash and 2 048 bytes of RAM. If your change
   touches firmware code, check the size summary PlatformIO prints at the end
   of the build and mention the new numbers in the pull request.

3. **Python tools**:

   ```sh
   pip install -r tools/requirements.txt -r tools/requirements-dev.txt
   ruff check tools
   ruff format --check tools
   mypy
   pytest
   ```

CI runs all of the above on every push and pull request.

## Guidelines

- Code in `lib/powercore` and `lib/ina219` must stay free of Arduino headers
  and of the C++ standard library (avr-gcc does not ship one). Use C headers
  such as `<stdint.h>`.
- New maths needs a native test with expected values derived by hand, with the
  derivation in a comment, not values copied from the implementation's output.
- If you change the telemetry frame, update the firmware encoder
  (`lib/powercore/src/powercore/telemetry.cpp`), the Python decoder
  (`tools/logger.py`), the shared fixtures in both test suites, and the format
  specification in the README. A change to the field list needs a new frame
  tag (`BM2`).
- Keep commits small, with an imperative subject line that says what changed.

## Reporting hardware results

The README states which parts of the project have been tested on real devices.
If you run the firmware on a board, please open an issue with the board, the
INA219 breakout, the shunt value, and what you observed (including anything
that did not work) so that section can be kept accurate.
