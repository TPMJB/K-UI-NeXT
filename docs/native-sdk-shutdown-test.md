# VMU management and Smash Pack continuation — @BUILD@

Copy the package's `KUI` folder onto the SD card and merge/replace
@INSTALL_CONTENTS@. Keep the existing K-ui installation and a copy of the
previous files for rollback. No installer is needed. Settings and save data
are not included in the update.

@GUI_SCOPE@

## VMU management

- **Delete one save:** VMU → Virtual VMUs → open an active card → View saves.
  Select the save and press **R Delete save**.
- **Delete a whole card:** VMU → Virtual VMUs → select the card → **X Card
  actions → Delete card**. From a game's **Matching cards** list, press
  **R Card actions** to reach the same actions.
- The confirmation starts on **Cancel**. It identifies the save or card and
  shows how many assigned games are affected. A shared card's saves are shared
  by all games using that card. Whole-card deletion also removes its game
  assignments. Confirm deletion only after checking that information.
- Stored snapshots allow reading and extracting individual saves. Activate a
  copy by assigning games before editing its individual saves. A whole stored
  snapshot can be removed through the explicit **Delete card** action.

Existing external-save import remains under **VMU → Backups & restore →
Y Import save**. Choose **VMS**, **VMI with its companion VMS**, or **DCI** from
SD. Import creates a verified SD backup bundle. Writing it to a physical VMU
is a separate explicit restore from the backups list.

## Smash Pack check

Use an **SD image**, **Background New (X)**, **Virtual VMU A2 On**, and
**CD music Off**. In Sega Smash Pack, select Shining Force and check whether
it passes the previous diagnostic stop and reaches the game. Optionally
check save/load in one game that already worked.

The original SDK continues polling after disabling Maple. This candidate
keeps forwarding that disabled polling and resumes the normal guarded path
when the SDK enables Maple again. It retains the original stopped-state
ownership checks and does not create replies or access storage while stopped.
Complete secondary-program compatibility still needs console testing.

**Quake III Arena is not claimed fixed.** Its actual Gin transfer overwrites
the resident worker during executable handoff. That requires a separate safe
handoff design and remains under investigation.

A1 and A2 builds are included for both the SD loader and GD Play; A2 remains
the default. The shared compatibility change carries to both launch paths.
The SD worker retains its 72 KiB reservation and full 192 KiB read-ahead cache.
GD Play retains its existing 80 KiB combined worker/support lease.

## WinCE and CDDA

Shared SCI stream, CRC, DMA ownership and resumable-sector handling already
apply to the transport. WinCE needs bounded data work and correct driver
wakeups that preserve its virtual-buffer and audio ownership. Native AICA/G2
and WinCE DirectSound require separate scheduling. This package changes
neither WinCE nor CDDA playback.

## Source and validation

`details/CANDIDATE.json` binds the clean source commit, installed files,
linked artifacts and supplied verification reports. `SHA256SUMS.txt` covers
every other package file. The reports state which host and compiled SH checks
ran; console behavior and the complete historical aggregate audit are not
claimed. No game data is included.

Recover the complete source offline with:

```sh
python3 details/source/checkout-source.py
```

With the pinned SH toolchain and KallistiOS configured, rebuild from
`details/restored-source`:

```sh
make -f Makefile.dc build/kui-runtime.elf -j2
make -f Makefile.vmu-native-background-compact BUILD=build/shutdown-a2 -j1
make -f Makefile.vmu-native-background-compact-a1 BUILD=build/shutdown-a1 -j1
make -f Makefile.disc-vmu BUILD=build/shutdown-disc-a2 -j1
make -f Makefile.disc-vmu BUILD=build/shutdown-disc-a1 DISC_VMU_VIRTUAL_A1=1 -j1
```

The package tool verifies exact ELF/binary/module/relocation bindings and
clean compile records. Supplying `--runtime-elf build/kui-runtime.elf` also
includes the matching GUI envelope, its ELF and compile record.
