#ifndef APP_UI_H
#define APP_UI_H

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after ui_init() while LVGL lock is held */
void app_ui_init_runtime_overlays(void);

/* Screen navigation that does not depend on generated ui.c helpers */
void app_ui_swipe_to_dir(lv_dir_t dir);

/* Runtime loading overlays on main screen */
void app_ui_set_loading_spinner_visible(bool visible);
void app_ui_set_loading_progress(bool visible, int percent);
void app_ui_set_info_background_art(const lv_img_dsc_t *art_dsc);
void app_ui_set_main_startup_status(const char *status_text);
void app_ui_finish_main_startup_status(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_H */
