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
cached-clock readback before reporting success.

The Dreamcast RTC has no timezone field. Its Unix-style timestamp already
represents local wall time, so the file-date adapter does not apply another
timezone conversion. FAT supports 1980 through 2107, but the Dreamcast's RTC ends
at `2086-02-06 06:28:15`; the console adapter rejects later values.

The API and range follow the pinned upstream KOS sources:

- [RTC contract](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/rtc.h)
- [Dreamcast RTC range](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/rtc.h)
- [Setter and cached-clock update](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/rtc.c)

Host checks cover every FAT date, leap-year boundaries, invalid inputs, the
explicit RTC setter's failure paths, and creation/modification dates surviving
remount on real FAT32 and exFAT images. Both image filesystems pass their checker.
Console acceptance is pending: compare the startup clock line with the BIOS
clock, create a small diagnostic report, and check its date on a PC. No new rip
or clock adjustment is needed when the existing clock is already correct.
