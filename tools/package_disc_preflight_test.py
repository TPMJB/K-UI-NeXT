#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the matching GD Play preflight-replay runtime and disc modules."""
import package_large_disc_test

if __name__ == "__main__":
    package_large_disc_test.GUIDE = "docs/gd-play-preflight-replay.md"
    package_large_disc_test.PURPOSE = "GD Play verified preflight replay console test"
    package_large_disc_test.main()
