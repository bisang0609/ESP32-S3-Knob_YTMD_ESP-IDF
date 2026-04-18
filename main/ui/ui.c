#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"

#include <inttypes.h>
#include <string.h>

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
    if (objects.wifi_status == NULL || text == NULL) {
        return;
    }

    lv_label_set_text_fmt(objects.wifi_status, "WiFi: %s", text);
}

void ui_set_ip_address_text(const char *text) {
    if (objects.ip_address == NULL || text == NULL) {
        return;
    }

    lv_label_set_text_fmt(objects.ip_address, "IP: %s", text);
}

void ui_set_retry_count_text(uint32_t retry_count) {
    if (objects.Retry == NULL) {
        return;
    }

    lv_label_set_text_fmt(objects.Retry, "Retry: %" PRIu32, retry_count);
}
