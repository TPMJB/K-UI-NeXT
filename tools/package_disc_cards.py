#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the inserted-disc card manager with its focused console guide."""
import package_medium_test

if __name__ == "__main__":
    package_medium_test.GUIDE = "docs/disc-card-linking.md"
    package_medium_test.main()
