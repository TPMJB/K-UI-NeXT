/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include "network_rip_posix.h"
static struct kui_network_rip remote_client;
static void register_remote(const struct kui_media_ops *unused){
    (void)unused;struct kui_media_ops media;kui_network_rip_media(&remote_client,&media);kui_media_set(&media);
}
/* Reuse the existing synthetic disc, cancellation, checkpoint, export and
 * engine assertions verbatim. Only its media registration changes. Its
 * local image FILE is never used for block I/O in this wrapper. */
#define kui_media_set register_remote
#define main local_fixture_main
#define KUI_CAPTURE_NO_REPAIR_FIXTURE 1
#include "capture_image.c"
#undef main
#undef kui_media_set
int main(int argc,char **argv){
    if(argc<6)return 2;
    struct kui_network_rip_config config;nr_config(&config,argv[1],argv[2],argv[3]);
    struct nr_posix posix={.fd=-1,.window=3072};struct kui_net_sockets net=nr_sockets(&posix);
    struct kui_network_rip_hooks hooks={.now_ms=nr_now,.pause=nr_pause};
    if(!kui_network_rip_open(&remote_client,&net,0,&config,&hooks)){
        fprintf(stderr,"Remote open failed: %s\n",kui_network_rip_problem(&remote_client));kui_network_rip_close(&remote_client);return 4;
    }
    int result=local_fixture_main(argc-3,argv+3);
    assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);kui_network_rip_close(&remote_client);
    return result;
}
