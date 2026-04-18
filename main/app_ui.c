#include "app_ui.h"

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ui/screens.h"
#include "ui/ui.h"
#include "ytmd_client.h"

static lv_obj_t *s_spinner = NULL;
static lv_obj_t *s_progress = NULL;
static lv_obj_t *s_info_spinner = NULL;
static lv_obj_t *s_info_bg_art = NULL;
static lv_obj_t *s_bound_art2 = NULL;
static lv_obj_t *s_bound_pause = NULL;
static bool s_pause_overlay_visible = false;
static bool s_main_startup_finished = false;

static const char *TAG = "app_ui";

typedef struct {
    ytmd_cmd_t cmd;
} ui_cmd_task_arg_t;

static bool is_valid_obj(lv_obj_t *obj)
{
    return obj && lv_obj_is_valid(obj);
}

static void set_main_startup_widgets_visible(bool visible)
{
    if (is_valid_obj(objects.prg1)) {
        if (visible) {
            lv_obj_clear_flag(objects.prg1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(objects.prg1);
        } else {
            lv_obj_add_flag(objects.prg1, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (is_valid_obj(objects.prg2)) {
        if (visible) {
            lv_obj_clear_flag(objects.prg2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(objects.prg2);
        } else {
            lv_obj_add_flag(objects.prg2, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (is_valid_obj(objects.ap_status)) {
        if (visible) {
            lv_obj_clear_flag(objects.ap_status, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(objects.ap_status);
        } else {
            lv_obj_add_flag(objects.ap_status, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void pause_overlay_set_visible(bool visible)
{
    s_pause_overlay_visible = visible;
    if (!is_valid_obj(objects.pause)) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(objects.pause, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(objects.pause);
    } else {
        lv_obj_add_flag(objects.pause, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_cmd_task(void *arg)
{
    ui_cmd_task_arg_t *ctx = (ui_cmd_task_arg_t *)arg;
    if (!ctx) {
        vTaskDelete(NULL);
        return;
    }

    ytmd_cmd_t cmd = ctx->cmd;
    free(ctx);

    esp_err_t ret = ytmd_client_send_command(cmd);
    ESP_LOGI(TAG, "ui command %d -> %s", (int)cmd, esp_err_to_name(ret));
    vTaskDelete(NULL);
}

static void send_cmd_async(ytmd_cmd_t cmd)
{
    ui_cmd_task_arg_t *ctx = (ui_cmd_task_arg_t *)malloc(sizeof(ui_cmd_task_arg_t));
    if (!ctx) {
        ESP_LOGW(TAG, "send_cmd_async oom");
        return;
    }
    ctx->cmd = cmd;

    BaseType_t ok = xTaskCreate(ui_cmd_task, "ui_cmd", 3 * 1024, ctx, 3, NULL);
    if (ok != pdPASS) {
        ESP_LOGW(TAG, "send_cmd_async task create failed");
        free(ctx);
    }
}

static void info_art2_click_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "art2 clicked -> pause");
    if (s_pause_overlay_visible) {
        return;
    }
    pause_overlay_set_visible(true);
    send_cmd_async(YTMD_CMD_PAUSE);
}

static void info_pause_click_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "pause icon clicked -> play");
    if (!s_pause_overlay_visible) {
        return;
    }
    pause_overlay_set_visible(false);
    send_cmd_async(YTMD_CMD_PLAY);
}

static void hide_generated_loading_widgets(void)
{
    if (is_valid_obj(objects.loadingbar)) {
        lv_obj_set_style_arc_color(objects.loadingbar, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_color(objects.loadingbar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(objects.loadingbar, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(objects.loadingbar, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_add_flag(objects.loadingbar, LV_OBJ_FLAG_HIDDEN);
    }
#ifdef LV_OBJ_FLAG_IGNORE_LAYOUT
    if (is_valid_obj(objects.loadingbar)) {
        lv_obj_add_flag(objects.loadingbar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
#endif
}

static void create_overlays_if_needed(void)
{
    if (!is_valid_obj(objects.main)) {
        return;
    }

    if (!is_valid_obj(s_spinner) || lv_obj_get_parent(s_spinner) != objects.main) {
        if (is_valid_obj(s_spinner)) {
            lv_obj_del(s_spinner);
        }
        s_spinner = lv_spinner_create(objects.main, 1000, 60);
        lv_obj_set_pos(s_spinner, 0, 0);
        lv_obj_set_size(s_spinner, 360, 360);
        lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_color(s_spinner, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_color(s_spinner, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_spinner, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_spinner, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_spinner);
    }

    if (!is_valid_obj(s_progress) || lv_obj_get_parent(s_progress) != objects.main) {
        if (is_valid_obj(s_progress)) {
            lv_obj_del(s_progress);
        }
        s_progress = lv_arc_create(objects.main);
        lv_obj_set_pos(s_progress, 0, 0);
        lv_obj_set_size(s_progress, 360, 360);
        lv_arc_set_range(s_progress, 0, 100);
        lv_arc_set_value(s_progress, 0);
        lv_arc_set_rotation(s_progress, 270);
        lv_arc_set_bg_angles(s_progress, 0, 360);
        lv_obj_remove_style(s_progress, NULL, LV_PART_KNOB);
        lv_obj_clear_flag(s_progress, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_color(s_progress, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_color(s_progress, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_progress, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_progress, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(s_progress, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(s_progress, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_add_flag(s_progress, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_progress);
    }

    if (is_valid_obj(objects.information) &&
        (!is_valid_obj(s_info_bg_art) || lv_obj_get_parent(s_info_bg_art) != objects.information)) {
        if (is_valid_obj(s_info_bg_art)) {
            lv_obj_del(s_info_bg_art);
        }

        s_info_bg_art = lv_img_create(objects.information);
        lv_obj_set_pos(s_info_bg_art, 0, 0);
        lv_obj_set_size(s_info_bg_art, 360, 360);
        lv_obj_clear_flag(s_info_bg_art, LV_OBJ_FLAG_CLICKABLE);
        /* 70% hazy background effect */
        lv_obj_set_style_img_recolor(s_info_bg_art, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor_opa(s_info_bg_art, (lv_opa_t)178, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(s_info_bg_art, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(s_info_bg_art);
    }

    if (is_valid_obj(objects.information) && is_valid_obj(objects.seek) &&
        (!is_valid_obj(s_info_spinner) || lv_obj_get_parent(s_info_spinner) != objects.information)) {
        if (is_valid_obj(s_info_spinner)) {
            lv_obj_del(s_info_spinner);
        }

        s_info_spinner = lv_spinner_create(objects.information, 1000, 60);
        lv_obj_clear_flag(s_info_spinner, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_color(s_info_spinner, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_color(s_info_spinner, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_info_spinner, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(s_info_spinner, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_add_flag(s_info_spinner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_info_spinner);
    }

    if (is_valid_obj(s_info_spinner) && is_valid_obj(objects.seek)) {
        int x = lv_obj_get_x(objects.seek);
        int y = lv_obj_get_y(objects.seek);
        int w = lv_obj_get_width(objects.seek);
        int h = lv_obj_get_height(objects.seek);
        if (w <= 0) w = 360;
        if (h <= 0) h = 360;
        lv_obj_set_pos(s_info_spinner, x, y);
        lv_obj_set_size(s_info_spinner, w, h);
    }

    if (is_valid_obj(objects.seek)) {
        /* seek arc is display-only; do not consume touch/click events */
        lv_obj_clear_flag(objects.seek, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(objects.seek, NULL, LV_PART_KNOB);
        /* Hide seek background track (black ring) */
        lv_obj_set_style_arc_opa(objects.seek, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        /* Use wine color for seek progress */
        lv_obj_set_style_arc_color(objects.seek, lv_color_hex(0x722F37), LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_opa(objects.seek, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(objects.seek, false, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_arc_rounded(objects.seek, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        /* Keep seek arc above album art (art2) on information screen */
        lv_obj_move_foreground(objects.seek);
    }

    if (is_valid_obj(objects.art2) && s_bound_art2 != objects.art2) {
        lv_obj_add_flag(objects.art2, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(objects.art2, info_art2_click_cb, LV_EVENT_CLICKED, NULL);
        s_bound_art2 = objects.art2;
    }

    if (is_valid_obj(objects.art2)) {
        lv_obj_set_style_border_width(objects.art2, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(objects.art2, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(objects.art2, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (is_valid_obj(objects.title)) {
        lv_obj_set_style_text_color(objects.title, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (is_valid_obj(objects.artist)) {
        lv_obj_set_style_text_color(objects.artist, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (is_valid_obj(objects.pause)) {
        if (s_bound_pause != objects.pause) {
            lv_obj_add_flag(objects.pause, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(objects.pause, info_pause_click_cb, LV_EVENT_CLICKED, NULL);
            s_bound_pause = objects.pause;
        }
        pause_overlay_set_visible(s_pause_overlay_visible);
    }
}

void app_ui_init_runtime_overlays(void)
{
    s_main_startup_finished = false;
    hide_generated_loading_widgets();
    create_overlays_if_needed();
    if (is_valid_obj(objects.ap_status)) {
        lv_label_set_text(objects.ap_status, "Find AP");
    }
    set_main_startup_widgets_visible(true);
}

void app_ui_swipe_to_dir(lv_dir_t dir)
{
    if (!is_valid_obj(objects.main) || !is_valid_obj(objects.information)) {
        return;
    }

    lv_obj_t *act = lv_scr_act();
    if (dir == LV_DIR_LEFT && act == objects.main) {
        loadScreen(SCREEN_ID_INFORMATION);
    } else if (dir == LV_DIR_RIGHT && act == objects.information) {
        loadScreen(SCREEN_ID_MAIN);
    }
}

void app_ui_set_loading_spinner_visible(bool visible)
{
    create_overlays_if_needed();
    if (!is_valid_obj(s_spinner)) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    }
}

void app_ui_set_loading_progress(bool visible, int percent)
{
    create_overlays_if_needed();
    if (!is_valid_obj(s_progress)) {
        return;
    }

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    lv_arc_set_value(s_progress, percent);

    if (visible) {
        lv_obj_clear_flag(s_progress, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_progress, LV_OBJ_FLAG_HIDDEN);
    }

    if (is_valid_obj(s_info_spinner)) {
        if (visible) {
            lv_obj_clear_flag(s_info_spinner, LV_OBJ_FLAG_HIDDEN);
            /* Keep info spinner above album art/art2 while loading */
            lv_obj_move_foreground(s_info_spinner);
        } else {
            lv_obj_add_flag(s_info_spinner, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (is_valid_obj(objects.seek)) {
        if (visible) {
            lv_obj_add_flag(objects.seek, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(objects.seek, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(objects.seek);
        }
    }

    if (visible) {
        pause_overlay_set_visible(false);
    }
}

void app_ui_set_info_background_art(const lv_img_dsc_t *art_dsc)
{
    create_overlays_if_needed();
    if (!is_valid_obj(s_info_bg_art)) {
        return;
    }

    if (art_dsc) {
        lv_img_set_src(s_info_bg_art, art_dsc);
        lv_obj_set_pos(s_info_bg_art, 0, 0);
        lv_obj_set_size(s_info_bg_art, 360, 360);
        lv_img_set_zoom(s_info_bg_art, 256);
        lv_obj_clear_flag(s_info_bg_art, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_background(s_info_bg_art);
        lv_obj_invalidate(s_info_bg_art);
    } else {
        lv_obj_add_flag(s_info_bg_art, LV_OBJ_FLAG_HIDDEN);
    }
}

void app_ui_set_main_startup_status(const char *status_text)
{
    create_overlays_if_needed();
    if (s_main_startup_finished) {
        return;
    }

    if (is_valid_obj(objects.ap_status) && status_text && status_text[0] != '\0') {
        lv_label_set_text(objects.ap_status, status_text);
    }

    set_main_startup_widgets_visible(true);
}

void app_ui_finish_main_startup_status(void)
{
    create_overlays_if_needed();
    s_main_startup_finished = true;
    set_main_startup_widgets_visible(false);
}
