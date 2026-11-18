# Security policy

## Supported versions

Only the latest release receives fixes.

## Reporting a vulnerability

Please report security problems privately through GitHub's
[private vulnerability reporting](https://github.com/projectbin07-stack/ina219-battery-monitor/security/advisories/new)
rather than in a public issue. Include the affected version or commit, what
you observed and, if possible, a way to reproduce it. You should get a reply
within two weeks.

## Scope notes

- The firmware accepts commands on its serial port without authentication.
  Anyone with physical or serial access to the device can reset the counters
  or change the calibration. This is by design for a bench instrument; do not
  expose the serial port to untrusted parties.
- The low-voltage cutoff output is a convenience feature, not a certified
  battery protection circuit. Do not rely on it as the only protection for a
  lithium cell; use a proper protection IC/BMS.
- `tools/logger.py` treats everything received from the serial port as
  untrusted input: lines that fail validation are counted and discarded.
