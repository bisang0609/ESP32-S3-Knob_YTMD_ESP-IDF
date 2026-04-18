#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"

#include <string.h>
#include <inttypes.h>
#include <stdio.h>

static int16_t currentScreen = -1;

static lv_obj_t *getLvglObjectFromIndex(int32_t index) {
    if (index == -1) {
        return 0;
    }
    return ((lv_obj_t **)&objects)[index];
}

void loadScreen(enum ScreensEnum screenId) {
    currentScreen = screenId - 1;
    lv_obj_t *screen = getLvglObjectFromIndex(currentScreen);
    lv_scr_load_anim(screen, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
}

void ui_init() {
    create_screens();
    loadScreen(SCREEN_ID_MAIN);

}

void ui_tick() {
    tick_screen(currentScreen);
}

void ui_set_wifi_status_text(const char *text) {
    if (objects.wifi_status) lv_label_set_text(objects.wifi_status, text);
}

void ui_set_ip_address_text(const char *text) {
    if (objects.ip_address) lv_label_set_text(objects.ip_address, text);
}

void ui_set_retry_count_text(uint32_t count) {
    if (objects.retry) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%" PRIu32, count);
        lv_label_set_text(objects.retry, buf);
    }
}