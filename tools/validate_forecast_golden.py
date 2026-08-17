#!/usr/bin/env python3
"""Cross-check Meteo forecast golden vectors from picoware-meteo tests."""

import sys
import os
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "picoware-meteo"))
sys.path.insert(0, os.path.join(ROOT, "src"))

from meteo import forecast as fc  # noqa: E402


def main():
    os.chdir(ROOT)
    loader = unittest.TestLoader()
    suite = loader.discover("tests", pattern="test_forecast.py")
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    print("\nGolden forecast spec:", "PASS" if result.wasSuccessful() else "FAIL")
    # spot checks for C++ port alignment
    slp = fc.sea_level_hpa(1000.0, 100.0, 20.0)
    z = fc.zambretti_index(slp, fc.TREND_STEADY, 6, False, True)
    print("sea_level_hpa(1000,100,20)=%.3f zambretti=%s phrase=%r" % (slp, z, fc.zambretti_phrase(z)))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
