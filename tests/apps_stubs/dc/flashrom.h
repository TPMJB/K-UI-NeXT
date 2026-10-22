#ifndef KUI_TEST_APPS_FLASHROM_H
#define KUI_TEST_APPS_FLASHROM_H
typedef struct {int language,audio,autostart;} flashrom_syscfg_t;
int flashrom_get_region(void);
int flashrom_get_syscfg(flashrom_syscfg_t *out);
int flashrom_info(int part,int *start,int *size);
int flashrom_read(int offset,void *out,int bytes);
#endif
