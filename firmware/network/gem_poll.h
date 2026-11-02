#ifndef GEM_POLL_H
#define GEM_POLL_H
#include "lwip/netif.h"
err_t gem_poll_init(struct netif *netif);
void gem_poll_input(struct netif *netif,unsigned budget);
void gem_poll_link(struct netif *netif);
#endif
