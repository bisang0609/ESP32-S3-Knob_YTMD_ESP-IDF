#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_MAIN = 1,
    SCREEN_ID_WIFI = 2,
    SCREEN_ID_INFORMATION = 3,
    _SCREEN_ID_LAST = 3
};

typedef struct _objects_t {
    lv_obj_t *main;
    lv_obj_t *wifi;
    lv_obj_t *information;
    lv_obj_t *album_art;
    lv_obj_t *ssid;
    lv_obj_t *ssid_name;
    lv_obj_t *taget_ip;
    lv_obj_t *ytmd_ip;
    lv_obj_t *keboard;
    lv_obj_t *connect_btn;
    lv_obj_t *saved_connections;
} objects_t;

extern objects_t objects;

void create_screen_main();
void tick_screen_main();

void create_screen_wifi();
void tick_screen_wifi();

void create_screen_information();
void tick_screen_information();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/