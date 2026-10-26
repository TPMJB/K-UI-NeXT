#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind new audit maps to the exact timer-worker and inherited-stack profile."""


def game_startup_profile(worker, resident=None):
    from check_native_controller_failure_witness import owned_timer_profile
    owned = owned_timer_profile(worker)
    if resident is not None:
        marker = resident[4].get('__kui_native_owner_stack_ip_hook')
        if bool(marker) != owned or marker is not None and marker != (1, 0, 0, 0xfff1):
            raise ValueError('Owned timer worker and exact inherited owner-stack low hook must be paired')
    return owned
