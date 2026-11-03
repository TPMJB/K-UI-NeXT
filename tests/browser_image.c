/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the existing real-FatFs/adapter models, file-handle accounting and
 * FTP client harness. No duplicated socket/card implementation. */
#define main kui_ftp_image_main
#include "ftp_image.c"
#undef main
#include "kui/browser.h"

static void browser_publish(const struct kui_browser_status *s) {
    if(s->state == KUI_BROWSER_READY && !test.ready) {
        assert(!test.wifi || wifi_model_listening(s->port));
        /* Even an accidental nested worker dispatch must fail before it
         * touches this service's mounted card or live network adapter. */
        struct kui_ftp_status ftp;
        struct kui_browser_status nested;
        kui_ftp_run(NULL, NULL, &ftp, NULL, NULL, NULL);
        kui_browser_run(NULL, NULL, &nested, NULL, NULL, NULL);
        assert(ftp.state == KUI_FTP_FAILED && nested.state == KUI_BROWSER_FAILED);
        assert(strstr(ftp.message, "current network") && strstr(nested.message, "current network"));
        test.ready = true;
        printf("BROWSER_READY port=%u url=%s adapter=%s\n", s->port, s->url, s->adapter);
    }
}
static void browser_seed(void) {
    seed(false);
    assert(kui_sd_connect() && kui_mount(&fs, log_line));
    const char *dirs[] = {"0:/KUI/screenshots", "0:/KUI/reports", "0:/KUI/reports/support",
        "0:/KUI/reports/support/REPORT-000001", "0:/KUI/reports/support/REPORT-000002.part",
        "0:/KUI/exports", "0:/KUI/exports/saves"};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);++i) assert(f_mkdir(dirs[i]) == FR_OK);
    put("0:/KUI/screenshots/Fish & Chips.bmp", "screen capture\n");
    put("0:/KUI/screenshots/KUI-000002.part", "unfinished screenshot\n");
    put("0:/KUI/reports/support/REPORT-000001/manifest.json", "{\"complete\":true}\n");
    put("0:/KUI/reports/support/REPORT-000002.part/log.txt", "not published\n");
    put("0:/KUI/exports/saves/Card.vmd", "explicitly exported card\n");
    put("0:/KUI/exports/saves/card.journal", "never shared\n");
    FIL file; UINT wrote;
    assert(f_open(&file, "0:/KUI/screenshots/large.bmp", FA_WRITE|FA_CREATE_NEW) == FR_OK);
    uint8_t bytes[4096];
    for(unsigned i=0;i<sizeof(bytes);++i) bytes[i]=(uint8_t)(i*37u);
    for(unsigned i=0;i<32;++i) assert(f_write(&file,bytes,sizeof(bytes),&wrote)==FR_OK && wrote==sizeof(bytes));
    assert(f_close(&file)==FR_OK);
    assert(f_mount(NULL,"0:",0)==FR_OK);
    kui_sd_disconnect();
}
int main(int argc, char **argv) {
    if(argc < 3 || (strcmp(argv[2], "browser-seed") && strcmp(argv[2], "browser-serve"))) return kui_ftp_image_main(argc, argv);
    setvbuf(stdout, NULL, _IOLBF, 0);
    test.image = fopen(argv[1], "r+b");
    assert(test.image);
    struct stat st; assert(!fstat(fileno(test.image), &st));
    test.blocks = (uint64_t)st.st_size / 512u;
    kui_clock_configure(fixed_clock, NULL, log_line);
    int result=0;
    if(!strcmp(argv[2], "browser-seed")) { browser_seed(); puts("SEEDED"); }
    else {
        assert(argc==4 || argc==5);
        bool wifi=argc==5 && !strcmp(argv[4],"wifi");
        test.wifi=wifi;
        struct w5500_model_options model={.absent=wifi};
        w5500_model_start(&model);
        struct wifi_model_options board={false,1,0,false,KWM_WIFI_ONLINE};
        if(wifi)wifi_model_start(&board);
        const struct kui_net_ports ports={&port,wifi?&wifi_model_port:NULL};
        const struct kui_browser_options options={(uint16_t)atoi(argv[3])};
        struct kui_browser_status status;
        kui_browser_run(&ports,&options,&status,log_line,cancel,browser_publish);
        printf("BROWSER_STOPPED state=%d requests=%u downloads=%u failures=%u bytes=%llu message=%s\n",
            status.state,status.requests,status.downloads,status.failures,(unsigned long long)status.bytes_out,status.message);
        if(wifi)wifi_model_stop();
        w5500_model_stop();
        result=status.state==KUI_BROWSER_STOPPED?0:1;
    }
    assert(!test.connected && !test.files && !test.dirs && !fclose(test.image));
    return result;
}
