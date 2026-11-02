#ifndef DRIVESENSE_LWIP_ARCH_H
#define DRIVESENSE_LWIP_ARCH_H
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#include "xil_printf.h"
#ifndef BYTE_ORDER
#define BYTE_ORDER 1234
#endif
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define LWIP_PLATFORM_DIAG(x) do{xil_printf x;}while(0)
void pc_net_assert(const char *message);
#define LWIP_PLATFORM_ASSERT(x) pc_net_assert(x)
#define LWIP_RAND() ((uint32_t)rand())
#endif
