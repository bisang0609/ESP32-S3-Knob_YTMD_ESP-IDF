#include "ui.h"
#include "screens.h"
#include "images.h"
#include "actions.h"
#include "vars.h"

#include <string.h>

static int16_t currentScreen = -1;
static const enum ScreensEnum k_swipe_screens[] = {
    SCREEN_ID_MAIN,
    SCREEN_ID_INFORMATION,
};
static const int k_swipe_screen_count = sizeof(k_swipe_screens) / sizeof(k_swipe_screens[0]);

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

static int get_swipe_order_index(enum ScreensEnum screen_id)
{
    for (int i = 0; i < k_swipe_screen_count; i++) {
        if (k_swipe_screens[i] == screen_id) {
            return i;
        }
    }
    return -1;
}

void ui_swipe_to_dir(lv_dir_t dir)
{
    enum ScreensEnum current = (enum ScreensEnum)(currentScreen + 1);
    int idx = get_swipe_order_index(current);
    if (idx < 0) {
        loadScreen(SCREEN_ID_MAIN);
        return;
    }

    int next_idx = idx;
    if (dir == LV_DIR_LEFT && idx < (k_swipe_screen_count - 1)) {
        next_idx = idx + 1;
    } else if (dir == LV_DIR_RIGHT && idx > 0) {
        next_idx = idx - 1;
    }

    if (next_idx != idx) {
        loadScreen(k_swipe_screens[next_idx]);
    }
}

void ui_init() {
    create_screens();
    loadScreen(SCREEN_ID_MAIN);

}

void ui_tick() {
    tick_screen(currentScreen);
}
