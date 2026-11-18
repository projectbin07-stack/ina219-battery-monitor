"""Make ``tools/logger.py`` importable as ``logger`` from the tests."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
