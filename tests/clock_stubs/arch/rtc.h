/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_RTC_H
#define KUI_TEST_RTC_H
#include <time.h>
time_t rtc_unix_secs(void);
int rtc_set_unix_secs(time_t value);
#endif
