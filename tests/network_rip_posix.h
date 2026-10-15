/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only socket adapter. Deliberately small send/receive windows exercise
 * the same fragmented, bounded socket interface as both SCI adapters. */
#ifndef KUI_TEST_NETWORK_RIP_POSIX_H
#define KUI_TEST_NETWORK_RIP_POSIX_H
#include "kui/network_rip.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
struct nr_posix { int fd;bool connecting;size_t window; };
static uint64_t nr_now(void *ctx){(void)ctx;struct timespec ts;assert(!clock_gettime(CLOCK_MONOTONIC,&ts));return (uint64_t)ts.tv_sec*1000u+(unsigned)ts.tv_nsec/1000000u;}
static void nr_pause(void *ctx,unsigned ms){(void)ctx;struct timespec ts={0,(long)(ms?ms:1)*1000000L};nanosleep(&ts,NULL);}
static bool nr_close(void *ctx,unsigned slot){struct nr_posix *p=ctx;(void)slot;if(p->fd>=0)close(p->fd);p->fd=-1;return true;}
static bool nr_connect(void *ctx,unsigned slot,uint16_t local,const uint8_t ip[4],uint16_t port){
    struct nr_posix *p=ctx;(void)slot;(void)local;(void)ip;
    nr_close(ctx,slot);p->fd=socket(AF_INET,SOCK_STREAM,0);if(p->fd<0)return false;
    int no_delay=1;if(setsockopt(p->fd,IPPROTO_TCP,TCP_NODELAY,&no_delay,sizeof(no_delay)))return false;
    if(fcntl(p->fd,F_SETFL,O_NONBLOCK)<0)return false;
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(port),.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
    int r=connect(p->fd,(struct sockaddr *)&address,sizeof(address));p->connecting=r<0;
    return !r||errno==EINPROGRESS;
}
static bool nr_state(void *ctx,unsigned slot,uint8_t *state){
    struct nr_posix *p=ctx;(void)slot;if(p->fd<0){*state=KUI_NET_CLOSED;return true;}
    if(p->connecting){
        struct pollfd poller={p->fd,POLLOUT,0};if(poll(&poller,1,0)<0)return false;
        if(!poller.revents){*state=KUI_NET_CONNECTING;return true;}
        int error=0;socklen_t n=sizeof(error);if(getsockopt(p->fd,SOL_SOCKET,SO_ERROR,&error,&n)||error){*state=KUI_NET_CLOSED;return true;}p->connecting=false;
    }
    char byte;ssize_t got=recv(p->fd,&byte,1,MSG_PEEK|MSG_DONTWAIT);
    *state=got==0?KUI_NET_PEER_CLOSED:KUI_NET_ESTABLISHED;
    return got>=0||errno==EAGAIN||errno==EWOULDBLOCK;
}
static bool nr_received(void *ctx,unsigned slot,size_t *out){struct nr_posix *p=ctx;(void)slot;int n;if(ioctl(p->fd,FIONREAD,&n)<0)return false;*out=(size_t)n;if(*out>p->window)*out=p->window;return true;}
static bool nr_receive(void *ctx,unsigned slot,void *data,size_t n){struct nr_posix *p=ctx;(void)slot;return recv(p->fd,data,n,0)==(ssize_t)n;}
static bool nr_room(void *ctx,unsigned slot,size_t *out){struct nr_posix *p=ctx;(void)slot;struct pollfd poller={p->fd,POLLOUT,0};if(poll(&poller,1,0)<0)return false;*out=poller.revents&POLLOUT?p->window:0;return true;}
static bool nr_send(void *ctx,unsigned slot,const void *data,size_t n){struct nr_posix *p=ctx;(void)slot;return send(p->fd,data,n,MSG_NOSIGNAL)==(ssize_t)n;}
static bool nr_keepalive(void *ctx,unsigned slot,unsigned seconds){(void)ctx;(void)slot;(void)seconds;return true;}
static struct kui_net_sockets nr_sockets(struct nr_posix *p){return (struct kui_net_sockets){.ctx=p,.count=1,.connect=nr_connect,.close=nr_close,.state=nr_state,.received=nr_received,.receive=nr_receive,.room=nr_room,.send=nr_send,.keepalive=nr_keepalive};}
static void nr_config(struct kui_network_rip_config *out,const char *port,const char *key,const char *volume){
    char text[512];int n=snprintf(text,sizeof(text),"host=10.0.0.2\nport=%s\nkey=%s\nvolume=%s\n",port,key,volume);
    assert(n>0&&(size_t)n<sizeof(text));assert(kui_network_rip_config_parse(out,text,(size_t)n));
}
#endif
