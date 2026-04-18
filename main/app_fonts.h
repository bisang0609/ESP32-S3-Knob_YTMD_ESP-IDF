#ifndef APP_FONTS_H
#define APP_FONTS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after ui_init() while LVGL lock is held. */
void app_init_fonts(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FONTS_H */
