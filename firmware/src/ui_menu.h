#ifndef UI_MENU_H
#define UI_MENU_H

#include "ui_settings.h"
#include "input.h"

void ui_menu_enter(ui_page_t *page);
void ui_menu_on_input(input_evt_t ev, ui_page_t *page);
void ui_menu_poll(ui_page_t page);
void ui_sensors_init(void);

#endif
