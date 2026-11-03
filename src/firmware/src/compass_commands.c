#if defined(OBSTACLE_CAPTURE_DEBUG)
#include "obstacle_service.h"
#endif
#include "app_features.h"
#if APP_ENABLE_MANUAL_DETECTION
#include "obstacle_service.h"
#endif
#include "compass_commands.h"
#include "compass_service.h"
#include "camera_snapshot.h"
#include "ui_clock.h"
#include <string.h>
#include <stdlib.h>
#ifndef COMPASS_COMMANDS_HOST_TEST
#include "xparameters.h"
#include "xuartps_hw.h"
#endif

static char g_line[48];
static unsigned g_length;
static int g_discard;

void compass_commands_init(void) { g_length = 0; g_discard = 0; }

static void execute(void)
{
    if (ui_clock_command(g_line)) return;
#if defined(OBSTACLE_CAPTURE_DEBUG)
    if (!strcmp(g_line,"OB CAPTURE")) {obstacle_service_debug_begin();return;}
#endif
    if (camera_snapshot_command(g_line)) return;
#if APP_ENABLE_MANUAL_DETECTION
    if (!strcmp(g_line,"OBSTACLE ON")) {obstacle_service_set_enabled(1);return;}
    if (!strcmp(g_line,"OBSTACLE OFF")) {obstacle_service_set_enabled(0);return;}
#endif
    if (!strcmp(g_line, "CAL BEGIN")) compass_service_begin_calibration();
    else if (!strcmp(g_line, "CAL END")) (void)compass_service_finish_calibration();
    else if (!strcmp(g_line, "CAL FLIP")) compass_service_flip_direction();
    else if (!strcmp(g_line, "CAL CANCEL")) compass_service_cancel_calibration();
    else if (!strcmp(g_line, "CAL CONFIRM")) compass_service_confirm_direction();
    else if (!strncmp(g_line, "CAL REF ", 8)) {
        char *end;
        long reference;
        /* strtol alone accepts signs/whitespace. Require decimal digits. */
        const char *p = g_line + 8;
        if (!*p) return;
        while (*p) { if (*p < '0' || *p > '9') return; ++p; }
        reference = strtol(g_line + 8, &end, 10);
        if (*end == '\0' && reference >= 0 && reference <= 3599)
            (void)compass_service_align_reference((int)reference);
    }
}

void compass_commands_feed(unsigned char byte)
{
    if (byte == '\r') return;
    if (byte == '\n') {
        if (!g_discard) { g_line[g_length] = '\0'; execute(); }
        compass_commands_init();
    } else if (byte < 32 || byte > 126 || g_length + 1 >= sizeof g_line) {
        g_discard = 1;
    } else if (!g_discard) g_line[g_length++] = (char)byte;
}

void compass_commands_poll(void)
{
#ifndef COMPASS_COMMANDS_HOST_TEST
    unsigned budget = 32;
    while (budget-- && XUartPs_IsReceiveData(STDOUT_BASEADDRESS)) {
        compass_commands_feed((unsigned char)XUartPs_ReadReg(
            STDOUT_BASEADDRESS, XUARTPS_FIFO_OFFSET));
    }
#endif
}
