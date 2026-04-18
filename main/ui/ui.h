#ifndef EEZ_LVGL_UI_GUI_H
#define EEZ_LVGL_UI_GUI_H

#include <lvgl.h>

#include "screens.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init();
void ui_tick();

void loadScreen(enum ScreensEnum screenId);
void ui_swipe_to_dir(lv_dir_t dir);

#ifdef __cplusplus
}
#endif

#endif // EEZ_LVGL_UI_GUI_H
