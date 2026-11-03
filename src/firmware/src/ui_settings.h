#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include "xil_types.h"
#include "input.h"
#include "app_features.h"

typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_MENU,
    UI_PAGE_ENV,
    UI_PAGE_COMPASS,
    UI_PAGE_LIGHT,
    UI_PAGE_POSE,
    UI_PAGE_IMU,
    UI_PAGE_VOICE,
    UI_PAGE_AMBIENT,
#if APP_ENABLE_TOF
    UI_PAGE_DISTANCE,
#endif
#if APP_ENABLE_MANUAL_DETECTION
    UI_PAGE_OBSTACLE,
#endif
} ui_page_t;

void ui_app_init(void);
ui_page_t ui_app_page(void);

void ui_app_on_input(input_evt_t ev);
void ui_app_on_second(void);
void ui_app_poll(void);

#endif
