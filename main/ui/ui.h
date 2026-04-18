#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include <stdint.h>

#include <lvgl.h>

#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init();
void ui_tick();
void ui_set_wifi_status_text(const char *text);
void ui_set_ip_address_text(const char *text);
void ui_set_retry_count_text(uint32_t retry_count);

void loadScreen(enum ScreensEnum screenId);

#ifdef __cplusplus
}
#endif

#endif // EEZ_LVGL_UI_GUI_H
