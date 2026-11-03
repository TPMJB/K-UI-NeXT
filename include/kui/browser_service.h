/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_BROWSER_SERVICE_H
#define KUI_BROWSER_SERVICE_H
#include "kui/browser.h"
/* Internal bounded single-client service. The caller owns mounted media,
 * the adapter and this socket, and clocks Wi-Fi outside step(). No threads,
 * global filesystem traversal, writes, or network configuration here. */
struct kui_browser_service;
struct kui_browser_service *kui_browser_service_create(
    const struct kui_net_sockets *net, unsigned socket, uint16_t port,
    struct kui_browser_status *status);
bool kui_browser_service_step(struct kui_browser_service *service, uint64_t now);
const char *kui_browser_service_error(const struct kui_browser_service *service);
/* Cancels any request/download, closes files/folders and the socket. */
void kui_browser_service_destroy(struct kui_browser_service *service);
#endif
