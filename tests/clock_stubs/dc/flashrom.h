#ifndef KUI_TEST_CLOCK_FLASHROM_H
#define KUI_TEST_CLOCK_FLASHROM_H
#define FLASHROM_PT_BLOCK_1 2
#define FLASHROM_B1_SYSCFG 0x05
#define FLASHROM_B1_PW_SETTINGS_1 0x80
#define FLASHROM_B1_IP_SETTINGS 0xe0
int flashrom_info(int part, int *start_out, int *size_out);
int flashrom_read(int offset, void *buffer_out, int bytes);
int flashrom_write(int offset, void *buffer, int bytes);
int flashrom_delete(int offset);
#endif
