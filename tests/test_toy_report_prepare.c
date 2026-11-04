/* SPDX-License-Identifier: GPL-3.0-only */
#define main retail_reference_main
#include "games_retail.c"
#undef main

int main(int argc,char **argv) {
    assert(argc==2);test.fault="valid";
    const uint32_t refused[]={KUI_RETAIL_READER_STANDARD,KUI_RETAIL_READER_ASYNC,
        KUI_RETAIL_READER_ASYNC_EAGER,KUI_GAMES_RETAIL_CE_PROBE,
        KUI_GAMES_RETAIL_CE_PROBE|KUI_GAMES_RETAIL_CE_EXCERPT|KUI_RETAIL_READER_ASYNC,
        KUI_GAMES_RETAIL_CDDA|KUI_RETAIL_READER_ASYNC,
        KUI_GAMES_RETAIL_CDDA|KUI_GAMES_RETAIL_VIRTUAL_A1};
    for(unsigned i=0;i<sizeof(refused)/sizeof(*refused);i++) {
        struct kui_runtime_image package={0};
        assert(!kui_games_retail_prepare_reader(selected,refused[i],&package,log_line,cancel));
        assert(!package.data && !test.connects && !test.read_calls && !test.writes);
    }
    struct stat st;assert(!stat(argv[1],&st) && !(st.st_size%512));
    test.image=fopen(argv[1],"r+b");assert(test.image);test.blocks=(uint64_t)st.st_size/512u;
    test.active=true;
    struct kui_runtime_image package={0};
    /* Missing saved preference is the existing VMU-On default. This must
     * refuse before reading any image or inherited loader module. */
    assert(!kui_games_retail_prepare_reader(selected,KUI_GAMES_RETAIL_CDDA,&package,log_line,cancel));
    assert(!package.data && test.connects && test.connects==test.disconnects && !test.connected &&
        !test.ordinary_package_opens && !test.dual_package_opens && !test.writes && !test.files);
    assert(!fclose(test.image));
    puts("PASS actual prepare_launch: native/CE/background/virtual-A1 refused before I/O; VMU On refused on mounted FAT volume");
    return 0;
}
