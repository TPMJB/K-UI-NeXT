#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the physical-VMU backup runtime with its focused console guide."""
import package_medium_test

if __name__ == "__main__":
    package_medium_test.GUIDE = "docs/vmu-menu-backup-recovery.md"
    package_medium_test.main()
