#ifndef DRIVESENSE_XLWIPCONFIG_H
#define DRIVESENSE_XLWIPCONFIG_H
/* lwIP core is used with our polling GEM netif, not the AMD IRQ adapter. */
#define XLWIP_CONFIG_INCLUDE_GEM 1
#endif
