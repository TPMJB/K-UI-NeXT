/* SPDX-License-Identifier: GPL-3.0-only */
/* Host preview uses the same embedded artwork/font and renderer as hardware.
 * Modes: home, ripper, confirm, settings, diagnostics, complete, partial,
 * stopped, destination, keyboard, advanced, ripper-settings, video, vmu,
 * memory, network, idle, reset, home-vmu, home-memory, home-network,
 * home-music, home-gd, music, gd-play, gd-confirm, quick-resume, dma-fallback,
 * music-queued, advanced-destination, clock, clock-confirm, defaults,
 * vmu-restore, vmu-restore-confirm, crc-scan, scan-folder, home-games,
 * games, games-variants, games-variants-2048, games-detail-2048, games-retail-2048,
 * games-detail, games-error, games-advanced, games-probe,
 * games-probe-loading, games-image-probe, games-image-probe-loading,
 * games-retail, games-retail-loading, games-retail-invalid, games-ce-probe, games-list-art,
 * games-retail-cd-plain, games-retail-cd-scrambled,
 * games-compact, games-gallery, games-scan, games-detail-art, home-files, home-ripper,
 * files, files-root, files-actions, files-actions-locked, files-pick,
 * files-copy, files-delete, files-refused, files-info, files-info-file,
 * files-view, files-copying, files-keyboard, network, ftp-starting, ftp-ready,
 * ftp-busy, ftp-stopped, ftp-failed, storage-tests, storage-test-confirm,
 * storage-test-busy, storage-test-result, storage-test-details,
 * storage-test-mismatch, storage-test-history, sci-async-probe, sci-async-result,
 * sci-async-failure, sci-async-dma-failure, sci-async-handoff-failure, sci-async-bus-failure,
 * sci-async-reset-failure, sci-async-stress-result, sci-async-stress-busy, sci-async-stress-failure,
 * sci-async-quiet-fault, ftp-wifi, wifi, wifi-online, wifi-joining,
 * wifi-password, wifi-name, wifi-forget, wifi-absent. */
#include "kui/shell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t frame[640*480];
static uint16_t covers[KUI_GAMES_ROWS][KUI_COVER_PIXELS], detail_cover[KUI_COVER_PIXELS];
static uint16_t picture[KUI_FILES_PICTURE_EDGE*KUI_FILES_PICTURE_EDGE];
static uint16_t rgb565(unsigned r,unsigned g,unsigned b) {
    return (uint16_t)((r>>3)<<11|(g>>2)<<5|(b>>3));
}
/* Abstract stand-in art for previews: shaded colour, a band and a disc.
 * No game artwork is embedded or reproduced. */
static void make_cover(uint16_t *out,unsigned edge,unsigned seed) {
    static const unsigned char hues[8][3]={{200,40,60},{40,110,200},{230,160,30},{60,170,90},
        {150,60,190},{30,160,170},{220,90,40},{90,90,120}};
    const unsigned char *c=hues[seed%8];
    for(unsigned y=0;y<edge;y++) for(unsigned x=0;x<edge;x++) {
        unsigned shade=255-(y*110/edge);
        unsigned r=c[0]*shade/255,g=c[1]*shade/255,b=c[2]*shade/255;
        int band=(int)x-(int)y+(int)(seed*7%edge)-(int)edge/3;
        if(band>=0 && band<(int)edge/6) {r=(r+255)/2;g=(g+255)/2;b=(b+255)/2;}
        int dx=(int)x-(int)(edge*2/3),dy=(int)y-(int)(edge*2/3),radius=(int)edge/5;
        if(dx*dx+dy*dy<radius*radius) {r=r/3;g=g/3;b=b/3;}
        if(y<edge/7) {r=20;g=24;b=40;}
        out[y*edge+x]=rgb565(r,g,b);
    }
}
static void library(struct kui_shell *shell,unsigned view) {
    static const char *names[]={"Fighting","Dead or Alive 2","Resident Evil - Code Veronica","MDK2",
        "Armada","Grandia II","Sword of the Berserk","Crazy Taxi"};
    static const char *titles[]={"","DEAD OR ALIVE 2","RESIDENT EVIL CODE:VERONICA","MDK2",
        "ARMADA","GRANDIA II","SWORD OF THE BERSERK","CRAZY TAXI"};
    struct kui_games_page *l=&shell->games_listing;
    shell->page=KUI_SHELL_GAMES;shell->games_view=view;
    l->count=8;l->total=43;l->has_more=true;l->artwork=true;l->view=view;
    unsigned edge=view==KUI_GAMES_VIEW_COMPACT?KUI_COVER_SMALL:view==KUI_GAMES_VIEW_GALLERY?KUI_COVER_MEDIUM:KUI_COVER_LARGE;
    for(unsigned i=0;i<8;i++) {
        struct kui_games_entry *e=&l->entries[i];
        snprintf(e->name,sizeof(e->name),"%s",names[i]);
        snprintf(e->title,sizeof(e->title),"%s",i?titles[i]:names[i]);
        e->directory=i==0;e->cover=i!=0 && i!=4;
        if(e->cover) make_cover(covers[i],edge,i);
    }
    shell->games_selected=1;
    strcpy(l->message,"Choose a game to inspect and launch.");
}
static void game_pair(struct kui_shell *shell,unsigned choice) {
    strcpy(shell->games_original_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
    strcpy(shell->games_2048_path,"/Games/Dead or Alive 2-2048/Dead or Alive 2.gdi");
    shell->games_variant_selected=choice;
    if(shell->page!=KUI_SHELL_GAMES_VARIANTS) {
        strcpy(shell->games_selected_path,choice?shell->games_2048_path:shell->games_original_path);
        strcpy(shell->games_detail.path,shell->games_selected_path);
    }
}
/* A game folder as the File Manager lists it: folders first, then files. */
static void files_folder(struct kui_shell *shell,bool root) {
    static const struct {const char *name;unsigned long long bytes;bool dir;unsigned char attr;} game[]={
        {"Saves",0,true,0},{"._track03.bin",4096,false,0x02},{"Dead or Alive 2.gdi",112,false,0},
        {"cover.png",251203,false,0},{"notes.txt",1834,false,0},{"track01.bin",1425312,false,0},
        {"track02.raw",2724048,false,0},{"track03.bin",1181616048ull,false,0}},
      top[]={{"Games",0,true,0},{"KUI",0,true,0},{"Music",0,true,0},{"System Volume Information",0,true,0x06},
        {"Pictures",0,true,0},{"autorun.inf",112,false,0x01},{"readme.txt",2048,false,0},{"setup.log",9021,false,0}};
    struct kui_files_page *l=&shell->files_listing;
    shell->page=KUI_SHELL_FILES;
    snprintf(shell->files_path,sizeof(shell->files_path),"%s",root?"/":"/Games/Fighting/Dead or Alive 2");
    snprintf(l->path,sizeof(l->path),"%s",shell->files_path);
    l->count=8;l->before=root?0:8;l->total=root?8:19;
    for(unsigned i=0;i<8;i++) {
        struct kui_files_entry *e=&l->entries[i];
        snprintf(e->name,sizeof(e->name),"%s",root?top[i].name:game[i].name);
        e->bytes=root?top[i].bytes:game[i].bytes;e->directory=root?top[i].dir:game[i].dir;
        e->attributes=root?top[i].attr:game[i].attr;
        e->date=(uint16_t)((46u<<9)|(9u<<5)|26u);e->time=(uint16_t)((14u<<11)|(3u<<5));
    }
    snprintf(l->first,sizeof(l->first),"%s",l->entries[0].name);l->first_directory=true;
    snprintf(l->last,sizeof(l->last),"%s",l->entries[7].name);
    shell->files_selected=root?1:7;
}
static void files_preview(struct kui_shell *shell,enum kui_files_op op,bool ready) {
    files_folder(shell,false);
    struct kui_files_preview *pv=&shell->files_preview;
    shell->files_job.op=op;
    snprintf(shell->files_job.source,sizeof(shell->files_job.source),"%s",op==KUI_FILES_OP_DELETE?
        "/Games/Fighting/Dead or Alive 2":"/Games/Fighting/Dead or Alive 2/track03.bin");
    if(op==KUI_FILES_OP_COPY) snprintf(shell->files_job.target,sizeof(shell->files_job.target),"/Backup/Fighting");
    pv->job=shell->files_job;pv->ready=ready;pv->status.complete=true;pv->status.passed=ready;
    pv->directory=op==KUI_FILES_OP_DELETE;pv->files=pv->directory?7:1;pv->folders=pv->directory?1:0;
    pv->bytes=pv->directory?1185767203ull:1181616048ull;pv->free_known=true;pv->free_bytes=21474836480ull;
    pv->date=(uint16_t)((46u<<9)|(9u<<5)|26u);pv->time=(uint16_t)((14u<<11)|(3u<<5));
    if(op==KUI_FILES_OP_COPY) {
        pv->renamed=true;snprintf(pv->job.name,sizeof(pv->job.name),"track03 (2).bin");
        snprintf(shell->files_job.name,sizeof(shell->files_job.name),"%s",pv->job.name);
    }
    if(!ready) snprintf(pv->status.message,sizeof(pv->status.message),"Not enough free space: needs 1.1 GB, 812.4 MB free");
    shell->page=KUI_SHELL_FILES_CONFIRM;
}
/* The FTP server's status in each state its page draws. */
static struct kui_ftp_status ftp;
static void ftp_state(struct kui_shell *shell,struct kui_shell_view *view,const char *mode) {
    shell->page=KUI_SHELL_FTP;view->ftp=&ftp;view->busy=true;
    memset(&ftp,0,sizeof(ftp));
    if(!strcmp(mode,"ftp-starting")) {
        snprintf(ftp.message,sizeof(ftp.message),"Asking the router for an address (DHCP)");
        return;
    }
    ftp.state=KUI_FTP_READY;ftp.link=true;ftp.port=KUI_FTP_PORT;
    ftp.ip[0]=192;ftp.ip[1]=168;ftp.ip[2]=1;ftp.ip[3]=50;
    snprintf(ftp.password,sizeof(ftp.password),"48217365");
    snprintf(ftp.adapter,sizeof(ftp.adapter),"W5500 on SCI at 12.5 MHz; 100 Mbit/s full duplex");
    if(!strcmp(mode,"ftp-busy")) {
        struct kui_ftp_client *c=ftp.clients;
        c[0].active=c[0].logged_in=c[0].receiving=true;c[0].ip[0]=192;c[0].ip[1]=168;c[0].ip[2]=1;c[0].ip[3]=20;
        snprintf(c[0].name,sizeof(c[0].name),"track03.bin");c[0].done=UINT64_C(301989888);c[0].rate=512000;
        c[1].active=c[1].logged_in=c[1].sending=true;c[1].ip[0]=192;c[1].ip[1]=168;c[1].ip[2]=1;c[1].ip[3]=20;
        snprintf(c[1].name,sizeof(c[1].name),"Harbor Lights.ogg");c[1].done=3355443;c[1].total=5452595;c[1].rate=466944;
        c[2].active=c[2].logged_in=true;c[2].ip[0]=192;c[2].ip[1]=168;c[2].ip[2]=1;c[2].ip[3]=20;
        ftp.files_in=6;ftp.files_out=2;ftp.bytes_in=UINT64_C(1288490188);ftp.bytes_out=9437184;ftp.connections=3;
        ftp.event_count=2;
        snprintf(ftp.events[0],sizeof(ftp.events[0]),"Received /Games/Dead or Alive 2/track02.raw (1.1 GB)");
        snprintf(ftp.events[1],sizeof(ftp.events[1]),"Created folder /Games/Dead or Alive 2");
        snprintf(ftp.last_in,sizeof(ftp.last_in),"Last up 1023 KiB/s: card 1041, net 1402, overlap 100%%, 12.5%% retried");
        snprintf(ftp.last_out,sizeof(ftp.last_out),"Last down 548 KiB/s: card 573, net 1402, overlap 99%%");
    }
    if(!strcmp(mode,"ftp-stopped") || !strcmp(mode,"ftp-failed")) {
        view->busy=false;
        bool failed=!strcmp(mode,"ftp-failed");
        ftp.state=failed?KUI_FTP_FAILED:KUI_FTP_STOPPED;
        snprintf(ftp.message,sizeof(ftp.message),"%s",failed?"No W5500 answered on the SCI port (read FF)":
            "The FTP server was stopped on the Dreamcast");
        if(!failed) {
            ftp.files_in=6;ftp.files_out=2;ftp.bytes_in=UINT64_C(1288490188);ftp.bytes_out=9437184;ftp.event_count=1;
            snprintf(ftp.events[0],sizeof(ftp.events[0]),"192.168.1.20 disconnected");
        }
    }
}
/* The Wi-Fi page in each state it draws. */
static void wifi_state(struct kui_shell *shell,struct kui_shell_view *view,const char *mode) {
    static const struct {const char *ssid;uint8_t channel,security;int8_t rssi;} nets[]={
        {"Home 5G",36,3,-48},{"Home",6,3,-57},{"Upstairs",44,4,-63},{"Neighbours",11,3,-71},{"Cafe",1,0,-80},
        {"Printer Direct",6,3,-84}};
    struct kui_wifi_view w;
    memset(&w,0,sizeof(w));
    shell->page=KUI_SHELL_WIFI;
    if(!strcmp(mode,"wifi-absent")) {
        w.failed=true;snprintf(w.message,sizeof(w.message),"No Wi-Fi board answered on the SCI port");
        kui_shell_set_wifi(shell,&w);
        return;
    }
    w.found=w.scanned=true;w.count=sizeof(nets)/sizeof(nets[0]);
    snprintf(w.board,sizeof(w.board),"XIAO ESP32-C5, firmware 0.1.0-3f2a9c1e (SPI 12.5 MHz, select GPIO6)");
    for(unsigned i=0;i<w.count;i++) {
        snprintf(w.networks[i].ssid,sizeof(w.networks[i].ssid),"%s",nets[i].ssid);
        w.networks[i].channel=nets[i].channel;w.networks[i].security=nets[i].security;w.networks[i].rssi=nets[i].rssi;
        w.networks[i].five=nets[i].channel>14;
    }
    w.wifi.band_mode=KWM_BAND_BOTH;
    snprintf(w.message,sizeof(w.message),"6 networks in range");
    if(strcmp(mode,"wifi")) {
        w.wifi.state=KWM_WIFI_ONLINE;w.wifi.saved=1;w.wifi.band=5;w.wifi.channel=36;w.wifi.rssi=-48;
        snprintf(w.wifi.ssid,sizeof(w.wifi.ssid),"Home 5G");
        w.wifi.ip[0]=192;w.wifi.ip[1]=168;w.wifi.ip[2]=1;w.wifi.ip[3]=23;
        snprintf(w.message,sizeof(w.message),"Online on Home 5G as 192.168.1.23; saved on the board");
    }
    if(!strcmp(mode,"wifi-joining")) {
        view->busy=true;w.working=true;
        w.wifi.state=KWM_WIFI_ASSOCIATED;snprintf(w.wifi.ssid,sizeof(w.wifi.ssid),"Upstairs");
        memset(w.wifi.ip,0,4);
        snprintf(w.message,sizeof(w.message),"Wi-Fi: joined Upstairs; waiting for an address");
    }
    kui_shell_set_wifi(shell,&w);
    shell->wifi_selected=strcmp(mode,"wifi-joining")?1:3;
    if(!strcmp(mode,"wifi-password")) {
        shell->wifi_selected=3;
        (void)kui_shell_input(shell,KUI_SHELL_A,false);
        snprintf(shell->keyboard,sizeof(shell->keyboard),"blue-Heron!42");
        shell->keyboard_layer=KUI_SHELL_KEYS_SYMBOLS;shell->keyboard_selected=12;
    }
    if(!strcmp(mode,"wifi-name")) {
        shell->wifi_selected=kui_shell_wifi_rows(shell)-1;
        (void)kui_shell_input(shell,KUI_SHELL_A,false);
        snprintf(shell->keyboard,sizeof(shell->keyboard),"Garage");
        shell->keyboard_selected=16;
    }
    if(!strcmp(mode,"wifi-forget")) (void)kui_shell_input(shell,KUI_SHELL_Y,false);
}
static unsigned home_row(enum kui_shell_page page) {
    for(unsigned i=0;i<KUI_SHELL_HOME_APPS;i++) if(kui_shell_home_pages[i]==page) return i;
    return 0;
}
static struct kui_storage_test_history test_history;
static struct kui_storage_test_progress test_progress;
static void storage_tests(struct kui_shell *s,struct kui_shell_view *v,const char *mode) {
    s->page=KUI_SHELL_STORAGE_TESTS;v->storage_test_target="SCI SD";
    s->storage_test_request.preset=KUI_STORAGE_TEST_COMPARE;s->storage_test_request.repeats=3;
    s->storage_test_selected=4;snprintf(s->storage_test_request.card_label,24,"Samsung 128GB");
    v->busy=false;
    if(!strcmp(mode,"storage-tests")) return;
    if(!strcmp(mode,"storage-test-confirm")) {s->confirm_storage_test=true;return;}
    if(!strcmp(mode,"storage-test-busy")) {
        test_progress=(struct kui_storage_test_progress){.preset=KUI_STORAGE_TEST_SOAK,.repeat=12,
            .done=2097152,.total=16777216,.elapsed_us=420000000,.target_us=900000000};
        snprintf(test_progress.phase,sizeof(test_progress.phase),"Verifying");
        v->storage_test_progress=&test_progress;v->busy=true;return;
    }
    memset(&test_history,0,sizeof(test_history));test_history.count=8;
    for(unsigned i=0;i<8;i++) {
        struct kui_storage_test_result *r=&test_history.rows[i];
        r->id=12-i;r->request=s->storage_test_request;r->saved=true;
        r->outcome=i==3?KUI_STORAGE_TEST_STOPPED:i==5?KUI_STORAGE_TEST_FAILED:KUI_STORAGE_TEST_PASSED;
        r->metadata.transport=i<4?1:0;r->metadata.cluster_bytes=131072;r->metadata.ui_hz=2;
        snprintf(r->metadata.filesystem,sizeof(r->metadata.filesystem),"exFAT");
        snprintf(r->metadata.build,sizeof(r->metadata.build),"%s",i<4?"a1b2c3d4e5f6":"112233445566");
        snprintf(r->path,sizeof(r->path),"0:/KUI/tests/t%06u",r->id);
        snprintf(r->message,sizeof(r->message),"Every saved byte matched after remount.");
        r->sample_count=9;r->cycles=9;r->elapsed_us=235000000;r->verified_bytes=144u*1048576;
        r->write_latency.max_us=84201;r->write_latency.p95_upper_us=20000;
        r->read_latency.max_us=19425;r->read_latency.p95_upper_us=20000;
        for(unsigned j=0;j<9;j++) r->samples[j]=(struct kui_storage_test_sample){
            .chunk_bytes=16384u<<(j%3*2),.repeat=j/3+1,.bytes=16u*1048576,
            .write_us=10000000+j*100000,.read_us=7000000+j*100000,.verified=true};
    }
    test_history.baseline_valid=true;test_history.baseline=test_history.rows[4];
    for(unsigned i=0;i<9;i++) {test_history.baseline.samples[i].read_us*=2;test_history.baseline.samples[i].write_us*=2;}
    kui_shell_set_storage_test_history(s,&test_history);
    if(!strcmp(mode,"storage-test-history")) {s->page=KUI_SHELL_STORAGE_TEST_HISTORY;return;}
    kui_shell_set_storage_test_result(s,&test_history.rows[0]);
    if(!strcmp(mode,"storage-test-details")) {
        s->storage_test_details=true;s->storage_test_result.outcome=KUI_STORAGE_TEST_FAILED;
        s->storage_test_result.errors.total=s->storage_test_result.errors.crc_errors=1;
        s->storage_test_result.errors.last_operation=KUI_STORAGE_ERROR_READ;
        s->storage_test_result.errors.last_result=KUI_STORAGE_ERROR_CRC;
        s->storage_test_result.errors.last_lba=1234567;s->storage_test_result.errors.last_count=128;
        s->storage_test_result.errors.sd_detail_valid=true;s->storage_test_result.errors.sd_command=18;
        s->storage_test_result.errors.sd_response=0x0b;s->storage_test_result.fatfs_error=1;
        snprintf(s->storage_test_result.failure_phase,24,"Verify read");
        s->storage_test_result.failure_offset=2097152;
        snprintf(s->storage_test_result.message,128,"Read failed during verification. See saved report.");
    }
    if(!strcmp(mode,"storage-test-mismatch")) s->storage_test_history.baseline.metadata.cluster_bytes=65536;
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    struct kui_settings preferences={true,false,true,KUI_CAPTURE_FORMAT_GDI};
    struct kui_shell shell; kui_shell_init(&shell,&preferences);
    const char *logs[]={"SD exFAT, 249997312 sectors, cluster=131072 bytes",
        "Volume start=2048 (MBR)","Disc: MDK2", "Track 04: audio",
        "Checkpoint saved. Partial job preserved.","Capture result: stopped",
        "Report saved: /KUI/probes/p0012/diagnostics.txt"};
    struct kui_shell_view view={.build="a1b2c3d4e5f6",.phase=2,.track=4,.tracks=31,
        .rate_kib=1012,.done=421ull*1048576,.total=1133ull*1048576,
        .committed=421ull*1048576,.elapsed_ms=426000,
        .phase_elapsed_ms=426000,.progress_age_ms=100,
        .memory_valid=true,.memory_used=2800*1024,.memory_physical=16384*1024,
        .memory_peak=2816*1024,.log_lines=logs,.log_count=7,.total_log_lines=174,
        .job_dir="/Games/MDK2 (2)",.disc_title="MDK2",.inserted_title="MDK2",
        .gdi_name="MDK2.gdi",.music_title="Neon Circuit",.music_enabled=true,
        .music_playing=true,.music_volume=75};
    struct kui_app_status status={.complete=true,.passed=true};
    view.music_cache_bytes=1111209;
    if(!strcmp(argv[1],"home-games")) shell.home_selected=home_row(KUI_SHELL_GAMES);
    else if(!strcmp(argv[1],"home-files")) shell.home_selected=home_row(KUI_SHELL_FILES);
    else if(!strcmp(argv[1],"home-ripper")) shell.home_selected=home_row(KUI_SHELL_RIPPER);
    else if(!strcmp(argv[1],"home-network")) shell.home_selected=home_row(KUI_SHELL_NETWORK);
    else if(!strcmp(argv[1],"network")) shell.page=KUI_SHELL_NETWORK;
    else if(!strcmp(argv[1],"ftp-wifi")) {
        ftp_state(&shell,&view,"ftp-ready");
        ftp.wifi=true;ftp.ip[3]=23;
        snprintf(ftp.adapter,sizeof(ftp.adapter),"Wi-Fi: Home 5G, 5 GHz channel 36, -48 dBm");
    } else if(!strncmp(argv[1],"ftp-",4)) ftp_state(&shell,&view,argv[1]);
    else if(!strncmp(argv[1],"wifi",4)) wifi_state(&shell,&view,argv[1]);
    else if(!strncmp(argv[1],"storage-test",12)) storage_tests(&shell,&view,argv[1]);
    else if(!strcmp(argv[1],"files") || !strcmp(argv[1],"files-root")) {
        files_folder(&shell,!strcmp(argv[1],"files-root"));
        if(!strcmp(argv[1],"files")) {
            snprintf(shell.files_notice,sizeof(shell.files_notice),"Copied to /Backup/Fighting");
            snprintf(shell.files_notice_detail,sizeof(shell.files_notice_detail),"7 files, 1.1 GB, read back and checked.");
        }
    } else if(!strcmp(argv[1],"files-actions") || !strcmp(argv[1],"files-actions-locked")) {
        files_folder(&shell,true);shell.page=KUI_SHELL_FILES_ACTIONS;shell.files_action_selected=1;
        if(!strcmp(argv[1],"files-actions-locked")) {shell.files_selected=1;shell.files_action_selected=4;}
        else shell.files_selected=0;
    } else if(!strcmp(argv[1],"files-pick")) {
        files_folder(&shell,false);shell.page=KUI_SHELL_FILES_PICK;
        shell.files_job.op=KUI_FILES_OP_COPY;
        snprintf(shell.files_job.source,sizeof(shell.files_job.source),"/Games/Fighting/Dead or Alive 2");
        snprintf(shell.files_pick_path,sizeof(shell.files_pick_path),"/Backup");
        const char *folders[]={"Dreamcast","Fighting","Racing","RPG"};
        shell.files_pick.count=4;shell.files_pick.total=4;shell.files_pick.folders_only=true;
        for(unsigned i=0;i<4;i++) {
            snprintf(shell.files_pick.entries[i].name,sizeof(shell.files_pick.entries[i].name),"%s",folders[i]);
            shell.files_pick.entries[i].directory=true;
        }
        shell.files_pick_selected=1;
    } else if(!strcmp(argv[1],"files-copy")) files_preview(&shell,KUI_FILES_OP_COPY,true);
    else if(!strcmp(argv[1],"files-delete")) files_preview(&shell,KUI_FILES_OP_DELETE,true);
    else if(!strcmp(argv[1],"files-refused")) files_preview(&shell,KUI_FILES_OP_COPY,false);
    else if(!strcmp(argv[1],"files-info") || !strcmp(argv[1],"files-info-file")) {
        bool file=!strcmp(argv[1],"files-info-file");
        files_preview(&shell,file?KUI_FILES_OP_COPY:KUI_FILES_OP_DELETE,true);
        shell.files_job.op=KUI_FILES_OP_DETAILS;shell.files_preview.job.op=KUI_FILES_OP_DETAILS;
        if(file) snprintf(shell.files_job.source,sizeof(shell.files_job.source),"/KUI/runtime.kui");
        if(file) {shell.files_preview.bytes=1834112;shell.files_preview.attributes=0x20;}
        shell.page=KUI_SHELL_FILES_INFO;
    } else if(!strcmp(argv[1],"files-view")) {
        shell.page=KUI_SHELL_FILES_VIEW;
        snprintf(shell.files_picture.path,sizeof(shell.files_picture.path),"/Pictures/Harbor at night.png");
        shell.files_picture.ok=true;shell.files_picture.format="PNG";
        shell.files_picture.width=1024;shell.files_picture.height=768;shell.files_picture.bytes=845120;
        make_cover(picture,KUI_FILES_PICTURE_EDGE,3);
    } else if(!strcmp(argv[1],"files-copying")) {
        files_folder(&shell,false);shell.files_job.op=KUI_FILES_OP_COPY;shell.files_running=true;view.busy=true;
        status=(struct kui_app_status){.done=1181616048ull,.total=2u*1185767203ull};
        snprintf(status.message,sizeof(status.message),"Checking 7 of 7: track03.bin");
        view.app_status=&status;
    } else if(!strcmp(argv[1],"files-keyboard")) {
        files_folder(&shell,false);shell.page=KUI_SHELL_KEYBOARD;shell.files_keyboard=true;
        shell.files_job.op=KUI_FILES_OP_MKDIR;shell.keyboard_selected=22;
        snprintf(shell.keyboard,sizeof(shell.keyboard),"Saves backup");
    }
    else if(!strcmp(argv[1],"games")) {
        shell.page=KUI_SHELL_GAMES;shell.games_listing.count=8;shell.games_listing.has_more=true;
        const char *names[]={"Fighting","Dead or Alive 2","Resident Evil - Code Veronica","MDK2",
            "Armada","Grandia II","Sword of the Berserk","A very long game name that clips safely at the right margin"};
        for(unsigned i=0;i<8;i++) snprintf(shell.games_listing.entries[i].name,sizeof(shell.games_listing.entries[i].name),"%s",names[i]);
        shell.games_listing.entries[0].directory=true;shell.games_selected=1;
        strcpy(shell.games_listing.message,"Choose a game image to inspect.");
    } else if(!strcmp(argv[1],"games-list-art")) library(&shell,KUI_GAMES_VIEW_LIST);
    else if(!strcmp(argv[1],"games-art-loading")) {
        library(&shell,KUI_GAMES_VIEW_LIST);shell.games_listing.entries[shell.games_selected].cover=false;
        shell.games_art_loading=true;
    } else if(!strcmp(argv[1],"games-rows-loading")) {
        library(&shell,KUI_GAMES_VIEW_LIST);shell.games_loading=true;view.busy=true;
        strcpy(shell.games_listing.message,"Reading SD directory...");
    }
    else if(!strcmp(argv[1],"games-variants") || !strcmp(argv[1],"games-variants-2048")) {
        library(&shell,KUI_GAMES_VIEW_LIST);shell.page=KUI_SHELL_GAMES_VARIANTS;
        game_pair(&shell,!strcmp(argv[1],"games-variants-2048"));
    }
    else if(!strcmp(argv[1],"games-compact")) library(&shell,KUI_GAMES_VIEW_COMPACT);
    else if(!strcmp(argv[1],"games-gallery")) {library(&shell,KUI_GAMES_VIEW_GALLERY);shell.games_selected=5;}
    else if(!strcmp(argv[1],"games-scan")) {
        shell.page=KUI_SHELL_GAMES;shell.games_scanning=true;view.busy=true;view.app_status=&status;
        status=(struct kui_app_status){.done=11,.total=43,.line_count=5};
        strcpy(status.message,"Box art 12 of 43: Grandia II");
        strcpy(status.lines[0],"Games found: 43");strcpy(status.lines[1],"New covers from discs: 9");
        strcpy(status.lines[2],"New covers from your images: 1");strcpy(status.lines[3],"No artwork found: 1");
        strcpy(status.lines[4],"Unchanged since last scan: 0");
    } else if(!strcmp(argv[1],"games-detail") || !strcmp(argv[1],"games-error") ||
            !strcmp(argv[1],"games-detail-art") || !strcmp(argv[1],"games-detail-2048")) {
        shell.page=KUI_SHELL_GAMES_DETAIL;
        strcpy(shell.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
        struct kui_games_detail *d=&shell.games_detail;
        strcpy(d->path,shell.games_selected_path);
        d->valid=strcmp(argv[1],"games-error")!=0;d->bytes=1185765648;d->tracks=3;d->data_tracks=2;d->audio_tracks=1;
        d->boot_bytes=123456;d->boot_lba=45166;d->native_gd=true;
        strcpy(d->title,"DEAD OR ALIVE 2");strcpy(d->product,"T-3601N");strcpy(d->region,"JUE");
        strcpy(d->boot_file,"1ST_READ.BIN");strcpy(d->message,"Track file missing: track03.bin");
        if(!strcmp(argv[1],"games-detail-art")) {d->cover=true;make_cover(detail_cover,KUI_COVER_LARGE,1);}
        if(!strcmp(argv[1],"games-detail-2048")) game_pair(&shell,1);
    } else if(!strcmp(argv[1],"games-advanced")) {
        shell.page=KUI_SHELL_GAMES_ADVANCED;shell.games_advanced_selected=2;
    } else if(!strcmp(argv[1],"games-probe") || !strcmp(argv[1],"games-probe-loading")) {
        shell.page=KUI_SHELL_GAMES_PROBE_CONFIRM;
        if(!strcmp(argv[1],"games-probe-loading")) {
            view.busy=true;view.app_status=&status;status.complete=false;
            strcpy(status.message,"Validating the probe package");
        }
    } else if(!strcmp(argv[1],"games-image-probe") || !strcmp(argv[1],"games-image-probe-loading")) {
        shell.page=KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM;
        strcpy(shell.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
        strcpy(shell.games_detail.path,shell.games_selected_path);shell.games_detail.valid=true;
        if(!strcmp(argv[1],"games-image-probe-loading")) {
            view.busy=true;view.app_status=&status;status.complete=false;
            strcpy(status.message,"Mapping selected image files");
        }
    }
    else if(!strcmp(argv[1],"games-retail") || !strcmp(argv[1],"games-retail-loading") ||
            !strcmp(argv[1],"games-retail-invalid") || !strcmp(argv[1],"games-ce-probe") ||
            !strcmp(argv[1],"games-retail-2048") || !strcmp(argv[1],"games-retail-cd-plain") ||
            !strcmp(argv[1],"games-retail-cd-scrambled")) {
        shell.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;
        strcpy(shell.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
        struct kui_games_detail *d=&shell.games_detail;
        strcpy(d->path,shell.games_selected_path);d->valid=true;d->tracks=3;
        strcpy(d->title,"DEAD OR ALIVE 2");strcpy(d->boot_file,"1ST_READ.BIN");
        d->boot_bytes=123456;d->boot_lba=45166;d->native_gd=true;
        if(!strcmp(argv[1],"games-retail-cd-plain") || !strcmp(argv[1],"games-retail-cd-scrambled")) {
            strcpy(shell.games_selected_path,"/Games/CD sample/disc.cue");strcpy(d->path,shell.games_selected_path);
            strcpy(d->title,"CD SAMPLE");d->native_gd=false;d->native_cd=d->cd_image=true;
            d->format=KUI_GAME_IMAGE_CUE;d->boot_lba=11716;
            shell.games_retail_scrambled=!strcmp(argv[1],"games-retail-cd-scrambled");
        }
        if(!strcmp(argv[1],"games-retail-2048")) game_pair(&shell,1);
        if(!strcmp(argv[1],"games-retail-loading")) {
            view.busy=true;view.app_status=&status;status.complete=false;
            strcpy(status.message,"Preparing selected game launch...");
        }
        if(!strcmp(argv[1],"games-retail-invalid")) d->native_gd=false;
        if(!strcmp(argv[1],"games-ce-probe")) {
            strcpy(shell.games_selected_path,"/Games/ARMADA/ARMADA.gdi");strcpy(d->path,shell.games_selected_path);
            strcpy(d->title,"ARMADA");strcpy(d->boot_file,"0WINCEOS.BIN");
            d->boot_bytes=1253376;d->boot_lba=548388;d->native_gd=false;d->windows_ce=true;d->tracks=5;
        }
    }
    else if(!strcmp(argv[1],"audio-cd")) {
        shell.page=KUI_SHELL_CD_AUDIO;shell.cd_audio.loaded=true;shell.cd_audio.count=12;
        shell.cd_audio.playing=true;shell.cd_audio.current=3;shell.cd_selected=2;
        view.music_title="Audio CD track 3";
        for(unsigned i=0;i<12;i++) {shell.cd_audio.tracks[i].number=i+1;shell.cd_audio.tracks[i].seconds=181+i*2;}
        strcpy(shell.cd_audio.message,"Playing track 3 from audio CD.");
    } else if(!strcmp(argv[1],"vmu-actions") || !strcmp(argv[1],"vmu-delete") || !strcmp(argv[1],"vmu-copy")) {
        shell.page=KUI_SHELL_VMU_ACTIONS;shell.vmu.present=true;shell.vmu.count=1;
        strcpy(shell.vmu.entries[0].name,"MDK2_SAVE");shell.vmu.entries[0].bytes=4096;
        shell.vmu_copy_slot=1;shell.confirm_vmu_delete=!strcmp(argv[1],"vmu-delete");
        shell.confirm_vmu_copy=!strcmp(argv[1],"vmu-copy");
    } else if(!strcmp(argv[1],"safe-area")) {
        shell.page=KUI_SHELL_SETTINGS;shell.system_selected=8;shell.system_draft.screen_inset=1;
    } else if(!strcmp(argv[1],"system-tools") || !strcmp(argv[1],"restart")) {
        shell.page=KUI_SHELL_SYSTEM_TOOLS;shell.confirm_restart=!strcmp(argv[1],"restart");
        view.app_status=&status;strcpy(status.message,"Settings flash backup verified on SD.");
        status.line_count=1;strcpy(status.lines[0],"/KUI/backups/system/flash-0001.bin");
    } else if(!strcmp(argv[1],"salvage") || !strcmp(argv[1],"salvage-confirm") || !strcmp(argv[1],"salvage-working")) {
        shell.page=KUI_SHELL_SALVAGE;shell.salvage_selected=3;shell.salvage_zero_fill=true;
        shell.confirm_salvage=!strcmp(argv[1],"salvage-confirm");
        view.app_status=&status;status.passed=false;strcpy(status.message,"Incomplete: 3 unresolved sectors remain.");
        if(!strcmp(argv[1],"salvage-working")) {
            view.busy=true;status.complete=false;status.line_count=6;status.done=5;status.total=12;
            strcpy(status.message,"Retrying unresolved sectors");
            const char *lines[]={"Track 3/31  FAD 45150  attempts 3","Targets 12  recovered 5  remaining 7",
                "Recovery pass 2/5  First pass: complete","/KUI/salvage/job-0001",
                "Unresolved zeros are NOT a verified game dump.","Separate recovery job; normal captures unchanged."};
            for(unsigned i=0;i<6;i++) snprintf(status.lines[i],KUI_APP_LINE_CAP,"%s",lines[i]);
        }
    } else if(!strcmp(argv[1],"retry")) {
        shell.page=KUI_SHELL_RIPPER;view.busy=true;view.retries=11;view.retry_attempt=3;view.retry_limit=10;view.retry_fad=45150;
    } else if(!strcmp(argv[1],"music-clear")) {
        shell.page=KUI_SHELL_MUSIC;shell.confirm_music_clear=true;
    } else if(!strcmp(argv[1],"clock") || !strcmp(argv[1],"clock-confirm")) {
        shell.page=KUI_SHELL_CLOCK;
        const struct kui_datetime d={2026,9,23,20,15,31};kui_shell_set_clock(&shell,&d,NULL);
        shell.clock_selected=2;shell.confirm_clock=!strcmp(argv[1],"clock-confirm");
    } else if(!strcmp(argv[1],"defaults")) {
        shell.page=KUI_SHELL_SETTINGS;shell.system_selected=7;shell.confirm_defaults=true;
    } else if(!strcmp(argv[1],"vmu-restore") || !strcmp(argv[1],"vmu-restore-confirm")) {
        shell.page=KUI_SHELL_VMU_RESTORE;shell.vmu_slot=5;shell.backups.total=12;shell.backups.count=8;
        for(unsigned i=0;i<8;i++) {
            snprintf(shell.backups.entries[i].name,16,"SAVE_%02u",i);
            snprintf(shell.backups.entries[i].folder,16,"v%04u",i+1);
        }
        shell.backup_selected=2;strcpy(shell.restore_name,"MDK2_SAVE");shell.restore_bytes=4096;
        strcpy(shell.backups.status.message,"Select a backup, then choose the target VMU.");
        shell.confirm_vmu_restore=!strcmp(argv[1],"vmu-restore-confirm");
    } else if(!strcmp(argv[1],"crc-scan")) {
        shell.page=KUI_SHELL_CRC_SCAN;strcpy(shell.browse_path,"/Games/ARMADA");
        view.app_status=&status;status.done=status.total=1187764704;status.line_count=7;
        strcpy(status.message,"Mode 1 checks passed; full catalogue match includes audio");
        strcpy(status.lines[0],"Track 5 / 5");
        strcpy(status.lines[1],"Data sectors 220386  Audio sectors 284616");
        strcpy(status.lines[2],"Bad sectors 0  Unsupported 0");
        strcpy(status.lines[3],"CRC mismatches 0  SHA mismatches 0");
        strcpy(status.lines[4],"TOSEC: FULL TRACK MATCH");
        strcpy(status.lines[5],"Armada v1.000 (1999)(Metro3D)(US)[!]");
        strcpy(status.lines[6],"No manifest; only FULL TRACK MATCH checks all audio too.");
    } else if(!strcmp(argv[1],"scan-folder")) {
        shell.page=KUI_SHELL_DESTINATION;shell.browse_for_scan=true;
        strcpy(shell.browse_path,"/Games/MDK2");
    } else if(!strcmp(argv[1],"home-music")) shell.home_selected=home_row(KUI_SHELL_MUSIC);
    else if(!strcmp(argv[1],"home-gd")) shell.home_selected=home_row(KUI_SHELL_GD_PLAY);
    else if(!strcmp(argv[1],"gd-play") || !strcmp(argv[1],"gd-confirm")) {
        shell.page=KUI_SHELL_GD_PLAY;shell.confirm_gd_boot=!strcmp(argv[1],"gd-confirm");
    } else if(!strcmp(argv[1],"quick-resume")) {
        shell.page=KUI_SHELL_ADVANCED;shell.advanced_selected=3;shell.confirm_quick_resume=true;
    } else if(!strcmp(argv[1],"music")) {
        shell.page=KUI_SHELL_MUSIC;shell.music_listing.count=8;shell.music_listing.has_more=true;
        const char *names[]={"Albums","Neon Circuit.wav","Orbital Drift.wav","Midnight Vector.wav",
            "Chrome Horizon.wav","Menu.wav","Long descriptive music filename that should clip safely.wav","Harbor Lights.ogg"};
        for(unsigned i=0;i<8;i++) snprintf(shell.music_listing.entries[i].name,
            sizeof(shell.music_listing.entries[i].name),"%s",names[i]);
        shell.music_listing.entries[0].directory=true;shell.music_selected=3;
        strcpy(shell.music_listing.message,"Choose a WAV or Ogg file to play.");
    } else if(!strcmp(argv[1],"settings")) {shell.page=KUI_SHELL_SETTINGS;shell.system_selected=2;}
    else if(!strcmp(argv[1],"ripper-settings")) shell.page=KUI_SHELL_RIPPER_SETTINGS;
    else if(!strncmp(argv[1],"ripper-format-",14)) {
        shell.page=KUI_SHELL_RIPPER_SETTINGS;shell.setting_selected=2;
        unsigned format=(unsigned)strtoul(argv[1]+14,NULL,10);
        if(format>=KUI_CAPTURE_FORMAT_COUNT) return 2;
        shell.draft.capture_format=(enum kui_capture_format)format;
    }
    else if(!strcmp(argv[1],"video")) {
        shell.page=KUI_SHELL_SETTINGS;view.video_trial=true;view.video_seconds=7;
        shell.system_draft.video_mode=KUI_VIDEO_PAL50;
    } else if(!strcmp(argv[1],"vmu")) {
        shell.page=KUI_SHELL_VMU;shell.vmu.count=8;shell.vmu.total=12;
        shell.vmu.present=true;shell.vmu.free_blocks=62;shell.vmu_selected=2;
        const char *names[]={"SONIC2__S01","MDK2___SAVE","BIOHAZARD_CV","BERSERK_SYS",
            "CRAZY_TAXI","SHENMUE_000","SOULCALIBUR","JETSETRADIO"};
        for(unsigned i=0;i<8;i++) {
            snprintf(shell.vmu.entries[i].name,16,"%s",names[i]);
            shell.vmu.entries[i].bytes=16384;
        }
        snprintf(shell.vmu.status.message,128,"12 saves found. Select a save to back up.");
    } else if(!strcmp(argv[1],"memory")) {
        shell.page=KUI_SHELL_MEMORY;view.app_status=&status;
        snprintf(status.message,128,"Allocated region passed");
        status.done=status.total=22ull*1024*1024;status.line_count=6;
        const char *lines[]={"Owned test region: 4096 KiB","Read coverage: 23068672 bytes; passes 70/70",
            "Errors: 0; first mismatch stops the test","6 full pattern/address passes",
            "64 walking-bit passes at 4 KiB block edges","CPU accesses; other RAM and VRAM are not tested"};
        for(unsigned i=0;i<6;i++) snprintf(status.lines[i],80,"%s",lines[i]);
    } else if(!strcmp(argv[1],"network")) {
        shell.page=KUI_SHELL_NETWORK;view.app_status=&status;status.passed=false;
        snprintf(status.message,128,"No Ethernet adapter detected");status.line_count=4;
        const char *lines[]={"Broadband and LAN adapters were checked.","No DHCP request or traffic sent.",
            "No reachable network is asserted.","Connect an adapter and inspect again."};
        for(unsigned i=0;i<4;i++) snprintf(status.lines[i],80,"%s",lines[i]);
    } else if(!strcmp(argv[1],"home-vmu")) shell.home_selected=home_row(KUI_SHELL_VMU);
    else if(!strcmp(argv[1],"home-memory")) shell.home_selected=home_row(KUI_SHELL_MEMORY);
    else if(!strcmp(argv[1],"home-network")) shell.home_selected=home_row(KUI_SHELL_NETWORK);
    else if(!strncmp(argv[1],"sci-async-",10)) {
        shell.page=KUI_SHELL_SCI_ASYNC_PROBE;view.app_status=&status;
        status=(struct kui_app_status){0};view.busy=false;
        if(!strcmp(argv[1],"sci-async-result") || !strcmp(argv[1],"sci-async-stress-result")) {
            status.complete=true;status.passed=true;status.line_count=8;
            snprintf(status.message,sizeof(status.message),"Verified reads with CPU work during DMA.");
            const char *probe_lines[]={"Slow: 16/16 reads verified; 16 DMA interrupts",
                "Fast: 64/64 reads verified; 64 DMA interrupts","CPU overlap batches: slow 4096 / fast 2048",
                "CRC OK  Data OK  Buffer guards OK","Normal read recovery: verified","Saved independent report:",
                "/KUI/tests/sci-async-0001/sci-async-probe.json","Normal game reads are unchanged by this experiment."};
            for(unsigned i=0;i<8;i++) snprintf(status.lines[i],KUI_APP_LINE_CAP,"%s",probe_lines[i]);
            if(!strcmp(argv[1],"sci-async-stress-result")) {
                snprintf(status.message,sizeof(status.message),"60-second stress passed with timer IRQs during DMA.");
                const char *stress_lines[]={"60s stress: 76543 reads; 16/16 sectors verified",
                    "CRC/data/guards OK; DMA interrupts 76543","CPU batches 346892; timer during DMA 2183",
                    "API max us: begin 108 poll 7 finish 92","Normal read recovery: verified",
                    "Saved independent report:","/KUI/tests/sci-async-0003/sci-async-probe.json",
                    "Display quiet verified; frames 0; skipped 120"};
                for(unsigned i=0;i<8;i++) snprintf(status.lines[i],KUI_APP_LINE_CAP,"%s",stress_lines[i]);
            }
        } else if(!strcmp(argv[1],"sci-async-stress-busy")) {
            view.busy=true;view.sci_video_quiet=true;
            snprintf(status.message,sizeof(status.message),"60-second stress: display updates paused.");
        } else if(!strcmp(argv[1],"sci-async-quiet-fault")) {
            status.complete=true;status.errors=1;status.line_count=8;
            snprintf(status.message,sizeof(status.message),"SCI receive error. Storage locked until restart.");
            const char *fault_lines[]={"Elapsed 0.503 s; reads 559; LBA 23456789",
                "Timer49 DMA29 QuietACK SQOK frames0","Pre SSRA6 SCRC0 OR00008201 event4E0",
                "DMA left 363 CHCR00004911 ERI1 RXI0","Normal read recovery: not attempted; restart required",
                "Report not saved; photograph this result.","PC8C21ABCD SR40000000 request 328 us",
                "DMA: DMA560 SSRA6 SPTR05 R100 TKFE"};
            for(unsigned i=0;i<8;i++) snprintf(status.lines[i],KUI_APP_LINE_CAP,"%s",fault_lines[i]);
        } else if(!strcmp(argv[1],"sci-async-failure") || !strcmp(argv[1],"sci-async-dma-failure") || !strcmp(argv[1],"sci-async-handoff-failure") || !strcmp(argv[1],"sci-async-bus-failure") || !strcmp(argv[1],"sci-async-reset-failure") || !strcmp(argv[1],"sci-async-stress-failure")) {
            status.complete=true;status.errors=1;status.line_count=8;
            snprintf(status.message,sizeof(status.message),"SCI unsupported state; reinit init failed. Restart required.");
            const char *probe_lines[]={"Slow: 0/1 reads verified; 0 DMA interrupts",
                "Fast: 0/0 reads verified; 0 DMA interrupts","CPU overlap batches: slow 0 / fast 0",
                "CRC unconfirmed  Data unconfirmed  Buffer guards unconfirmed",
                "Normal read recovery: FAILED - restart required","Report not saved; photograph this result.",
                "Recover init: card timeout; CMD0 R1FF","GPIO: DMA0 SSR84 SPTR82 R100 TKFE"};
            for(unsigned i=0;i<8;i++) snprintf(status.lines[i],KUI_APP_LINE_CAP,"%s",probe_lines[i]);
            if(!strcmp(argv[1],"sci-async-dma-failure")) {
                snprintf(status.message,sizeof(status.message),"SCI receive error. Storage locked until restart.");
                snprintf(status.lines[3],KUI_APP_LINE_CAP,"DMA left 513 CHCR00004911 ERI1 RXI0");
                snprintf(status.lines[6],KUI_APP_LINE_CAP,"No saved path");
                snprintf(status.lines[7],KUI_APP_LINE_CAP,"DMA: DMA1 SSR20 SPTR84 R100 TKFE");
            } else if(!strcmp(argv[1],"sci-async-handoff-failure")) {
                snprintf(status.message,sizeof(status.message),"SCI handoff failed; normal reads recovered after reinit.");
                const char *handoff_lines[]={"Slow 16/16 IRQ16  Fast 1/2 IRQ1",
                    "Handoff checks 2 retries 1 failures 1 faults 0",
                    "Handoff SSR86 SCRC8 SPTR05; CPU batches 176",
                    "DMA left 0 CHCR00004912 ERI0 RXI0","Normal read recovery: verified",
                    "Saved independent report:","/KUI/tests/sci-async-0002/sci-async-probe.json",
                    "handoff: DMA1 SSR86 SPTR05 R1FF TKFF"};
                for(unsigned i=0;i<8;i++) snprintf(status.lines[i],KUI_APP_LINE_CAP,"%s",handoff_lines[i]);
            } else if(!strcmp(argv[1],"sci-async-bus-failure") || !strcmp(argv[1],"sci-async-reset-failure") || !strcmp(argv[1],"sci-async-stress-failure")) {
                snprintf(status.message,sizeof(status.message),"SCI framing bus fault; reinit init failed. Restart required.");
                const char *bus_lines[]={"Slow 16/16 IRQ16  Fast 1/2 IRQ1",
                    "Framing ready poll index 0; faults 1",
                    "Before stop: wait40 SSR84 SCR30 SPTR05",
                    "DMA left 0 CHCR00004912 ERI0 RXI0","Normal read recovery: FAILED - restart required",
                    "Report not saved; photograph this result.","Recover init: card timeout; CMD0 R1FF",
                    "ready: DMA1 SSR84 SPTR05 R1FF TKFF"};
                for(unsigned i=0;i<8;i++) snprintf(status.lines[i],KUI_APP_LINE_CAP,"%s",bus_lines[i]);
                if(!strcmp(argv[1],"sci-async-reset-failure") || !strcmp(argv[1],"sci-async-stress-failure")) {
                    snprintf(status.lines[0],KUI_APP_LINE_CAP,"Slow16/16 IRQ16 Fast127/128 IRQ127 Reset128/128 fail0 state001");
                    snprintf(status.lines[1],KUI_APP_LINE_CAP,"Framing idle clock index 0; faults 1");
                    snprintf(status.lines[2],KUI_APP_LINE_CAP,"Before stop: wait40 SSR86 SCR30 SPTR05 STB00/01/00");
                    snprintf(status.lines[7],KUI_APP_LINE_CAP,"ready: DMA127 SSR84 SPTR05 R1FF TKFF");
                    if(!strcmp(argv[1],"sci-async-stress-failure")) {
                        snprintf(status.lines[0],KUI_APP_LINE_CAP,"Reads262143 IRQ262143 Reset262143/262144 F1 S110");
                        snprintf(status.lines[7],KUI_APP_LINE_CAP,"ready: DMA262143 SSR84 SPTR05 R1FF TKFF");
                    }
                }
            }
        }
    }
    else if(!strcmp(argv[1],"diagnostics")) shell.page=KUI_SHELL_DIAGNOSTICS;
    else if(!strcmp(argv[1],"advanced") || !strcmp(argv[1],"advanced-destination")) {
        shell.page=KUI_SHELL_ADVANCED;
        if(!strcmp(argv[1],"advanced-destination")) shell.advanced_selected=4;
    }
    else if(!strcmp(argv[1],"destination")) {
        shell.page=KUI_SHELL_DESTINATION;
        const char *folders[]={"Action","Adventure","Driving","Fighting","Imports",
            "Puzzle","Role-playing","Sports"};
        shell.listing.count=8; shell.listing.has_more=true; shell.browser_selected=3;
        for(unsigned i=0;i<8;i++) snprintf(shell.listing.entries[i].name,
            sizeof(shell.listing.entries[i].name),"%s",folders[i]);
    } else if(!strcmp(argv[1],"keyboard")) {
        shell.page=KUI_SHELL_KEYBOARD; shell.keyboard_selected=42;
        snprintf(shell.keyboard,sizeof(shell.keyboard),"/Games/Fighting");
    }
    else if(strcmp(argv[1],"home")) {
        shell.page=KUI_SHELL_RIPPER;
        if(!strcmp(argv[1],"confirm")) shell.confirm_new=true;
        else if(!strcmp(argv[1],"ripper-export")) {
            view.busy=true;view.phase=4;view.gdi_name="MDK2.chd";
        }
        else if(!strcmp(argv[1],"complete") || !strcmp(argv[1],"partial")) {
            view.phase=5; view.outcome=KUI_SHELL_OUTCOME_COMPLETE;
            view.done=view.total; view.committed=view.total;
            view.track=view.tracks; view.rate_kib=0;
            view.inserted_title="Sword of the Berserk";
            view.reference_checked=true;
            view.reference.result=!strcmp(argv[1],"complete")?
                KUI_KNOWN_FULL_MATCH:KUI_KNOWN_DATA_MATCH;
        } else if(!strcmp(argv[1],"idle")) {
            view.phase=0;view.done=view.total=view.committed=0;view.rate_kib=0;
            view.track=view.tracks=0;view.elapsed_ms=0;
        } else if(!strcmp(argv[1],"dma-fallback")) {
            view.busy=true;view.dma_degraded=true;view.rate_kib=690;
        } else if(!strcmp(argv[1],"music-queued")) {
            view.busy=true;view.music_change_pending=true;
        } else if(!strcmp(argv[1],"reset")) {
            view.outcome=KUI_SHELL_OUTCOME_FAILED;view.drive_reset_required=true;
            view.message="Capture timed out; drive abort failed. Restart required.";
        } else if(!strcmp(argv[1],"stopped")) view.outcome=KUI_SHELL_OUTCOME_STOPPED;
        else view.busy=true;
    }
    view.game_covers=(const uint16_t (*)[KUI_COVER_PIXELS])covers;view.game_detail_cover=detail_cover;
    view.files_picture=picture;
    kui_shell_draw(frame,&shell,&view,NULL,NULL);
    FILE *out=fopen(argv[2],"wb"); if(!out) return 1;
    if(fprintf(out,"P6\n640 480\n255\n")<0) return 1;
    for(unsigned i=0;i<640*480;i++) {
        unsigned v=frame[i];
        unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),
            (unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
        if(fwrite(rgb,1,3,out)!=3) return 1;
    }
    return fclose(out)!=0;
}
