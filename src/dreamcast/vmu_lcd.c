/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_lcd.h"
#include "kui/version.h"
#include <dc/maple.h>
#include <dc/maple/vmu.h>

void kui_vmu_lcd_startup(void) {
    static struct kui_vmu_lcd_bitmap bitmap;
    kui_vmu_lcd_render(&bitmap,KUI_VERSION);
    /* KOS completes its initial Maple scan before main. This helper is called
     * once after the first video frame and the existing bounded disc STOP.
     * LCD writes are copied/queued by KOS, not waited on. Busy/absent LCDs
     * are skipped; memory-card blocks and save files are never accessed. */
    for(unsigned n=0;n<MAPLE_PORT_COUNT*MAPLE_UNIT_COUNT;n++) {
        maple_device_t *device=maple_enum_type((int)n,MAPLE_FUNC_LCD);
        if(!device)break;
        (void)vmu_draw_lcd_rotated(device,bitmap.words);
    }
}
