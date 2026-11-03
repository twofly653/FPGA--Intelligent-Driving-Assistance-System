#ifndef APP_FEATURES_H
#define APP_FEATURES_H

/* Ethernet/GPU candidate only. The frozen R14 ELF remains untouched. */
#ifndef APP_ENABLE_PC_NETWORK
#define APP_ENABLE_PC_NETWORK 0
#endif

/* Photo convergence wait is a separate candidate; accepted R8 is retained. */
#ifndef APP_ENABLE_PHOTO_SETTLE
#define APP_ENABLE_PHOTO_SETTLE 0
#endif

/* Manual user control is opt-in; accepted R7 remains a separate build. */
#ifndef APP_ENABLE_MANUAL_DETECTION
#define APP_ENABLE_MANUAL_DETECTION 0
#endif

#ifndef APP_ENABLE_WEB_SYNC
#define APP_ENABLE_WEB_SYNC 0
#endif

/* New motion overlay is an explicit candidate build; accepted R5 stays default. */
#ifndef APP_ENABLE_OBSTACLES
#define APP_ENABLE_OBSTACLES 0
#endif

#ifndef APP_ENABLE_TOF
#define APP_ENABLE_TOF 1
#endif

/* ASCII telemetry for the PC dashboard; nonblocking UART at 25 Hz. */
#ifndef APP_ENABLE_TELEMETRY
#define APP_ENABLE_TELEMETRY 1
#endif

#endif
