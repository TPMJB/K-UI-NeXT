#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Keep the withdrawn updater release recipe from producing more packages."""

RELEASE_BLOCK_REASON = (
    "Offline updater release packaging is disabled: the Standard VMU recipe is wrong, "
    "and the presence-sensitive loader inventory has not been matched to a verified "
    "accepted baseline. The 28cadccf0e71 -> 33b04057db86 test ZIP is withdrawn."
)


def require_verified_updater_recipe():
    """No override: re-enable only after correcting and verifying the recipe."""
    raise ValueError(RELEASE_BLOCK_REASON)
