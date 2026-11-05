/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_UPDATE_SOFTWARE_FILE_H
#define KUI_UPDATE_SOFTWARE_FILE_H
#include "ff.h"

/* GUI-only FatFs link wrappers. Read-only files in the updater's bounded
 * software namespace follow the checked selection. Other paths and every
 * write retain ordinary FatFs behavior. Raw pins are verified on the same
 * open file before its cursor is restored for the caller. */
FRESULT __wrap_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_stat(const TCHAR *,FILINFO *);
/* Updater storage operations already selected an explicit physical source.
 * Root engine checks and snapshot copies must not re-resolve that source
 * through the GUI's currently running software selection. */
FRESULT kui_update_physical_open(FIL *,const TCHAR *,BYTE);
FRESULT kui_update_physical_stat(const TCHAR *,FILINFO *);
#endif
