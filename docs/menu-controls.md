<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Menu controls

Open **Settings → Controls** to adjust menu navigation. UP/DOWN selects a
preference and LEFT/RIGHT changes its value. A saves; X restores the normal
input values. B returns to Settings with the draft available, and B from
Settings discards unsaved changes.

| Preference | Normal | Other choices |
| --- | --- | --- |
| Navigation repeat | 250 ms initial delay, 80 ms between steps | Slow: 400/120 ms; Fast: 180/50 ms |
| Analog stick deadzone | 48 | Small: 32; Large: 64 |

The saved controls apply to K-UI menus. Action buttons remain single presses;
there is no game-wide controller remapping. Existing settings records retain
their preferences and use Normal for both new fields. Version 4 keeps the
same 40-byte, alternating-record format and rejects unknown values and
reserved bytes. A pending video-mode change still requires the existing
preview and confirmation before any system preferences are saved.
