# Console clock and SD file dates

The original diagnostic build disabled FatFs's RTC callback and supplied a fixed
September 2026 date. That setting also affected later ripper files; it did not
mean the console clock or its battery was wrong.

New files and folders now use the console's local date and time. KOS supplies a
cached RTC sample plus elapsed time, so updating a checkpoint does not need a
hardware clock read. FAT records seconds in two-second steps. Creation dates
remain the creation date when an existing file is updated; old dump dates are
not guessed or rewritten.

At startup the diagnostic log records the local clock value. An unavailable or
out-of-range clock produces an explicit warning and uses `1980-01-01`, the FAT
minimum, for subsequent file timestamps. It never silently substitutes the build
date. Clock startup and file creation do not set the hardware RTC. The Settings
clock editor writes it only after confirmation and checks the hardware and
cached-clock readback before reporting success. It also updates and reads back
the BIOS's last-set timestamp in flash. Updating the RTC alone left that record
stale and could trigger the BIOS date/time dialog on the next boot, as reported
on 2026-10-01.

The BIOS timestamp is bytes 2–5 of partition 2's logical system configuration
record (block 5), in little-endian seconds since 1950. K-UI copies the newest
CRC-valid record, changes only that timestamp and its CRC, then appends it to
an erased slot. Language, sound, autostart, unknown fields, previous records
and network settings remain intact. The allocation bitmap is reserved first,
so an interrupted write consumes a slot while leaving the previous valid
record available. Both the bitmap and the complete new record are read back.
K-UI never erases or compacts flash. It bounds the 16 KiB partition to its 254
data slots and refuses a missing/invalid record, malformed allocation bitmap,
dirty free slot or full partition before changing the RTC. A matching saved
timestamp needs no new slot. A later write failure reports an incomplete update
and directs the user to set the clock in the Dreamcast BIOS; the RTC may already
have changed. Ordinary startup, file timestamps and clock reload remain read-only.

The Dreamcast RTC has no timezone field. Its Unix-style timestamp already
represents local wall time, so the file-date adapter does not apply another
timezone conversion. FAT supports 1980 through 2107, but the Dreamcast's RTC ends
at `2086-02-06 06:28:15`; the console adapter rejects later values.

The API and range follow the pinned upstream KOS sources:

- [RTC contract](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/rtc.h)
- [Dreamcast RTC range](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/rtc.h)
- [Setter and cached-clock update](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/rtc.c)
- [BIOS system configuration record](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/flashrom.c)
- [Flash allocation and CRC format](https://mc.pp.se/dc/flashmem.html)
- [Existing openMenu RTC/SYSCFG synchronization](https://github.com/DerekPascarella/openMenu-Virtual-Folder-Bundle/blob/9253f15a8c6f7c5ac60f1666ce173c8773c0dac2/openMenu/src/openmenu_settings/src/openmenu_savefile.c#L615)

The synchronization follows the documented record format and existing homebrew
practice; the exact predicate used by every retail BIOS has not been established.
Flash calls accept the byte-count convention documented by KOS and the zero-on-
success convention used by BIOS replacements, with write readback required.

Host checks cover every FAT date, leap-year boundaries, invalid inputs, the
explicit RTC setter's failure paths, and creation/modification dates surviving
remount on real FAT32 and exFAT images. Both image filesystems pass their checker.
The platform fixture models flash writes as 1-to-0 only and checks forward and
backward clock edits, BIOS record preservation, allocation bounds, CRC handling,
write interruptions and readback failures.
Console acceptance is pending: compare the startup clock line with the BIOS
clock, create a small diagnostic report, and check its date on a PC. No new rip
or clock adjustment is needed when the existing clock is already correct.
For the next-boot regression, set the correct local time in K-UI, power-cycle,
and confirm that the BIOS does not ask for it again and its other settings are
unchanged. This fix needs only an SD runtime replacement; the `6af5e11` boot CD
and the crimson startup artwork remain current.
