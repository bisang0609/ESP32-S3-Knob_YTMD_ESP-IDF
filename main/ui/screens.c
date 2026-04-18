#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;

// Global state variables

static lv_anim_t anim;
static bool anim_initialized;

//
// Helper functions
//

lv_anim_t *get_anim() {
    if (!anim_initialized) {
        lv_anim_init(&anim);
        lv_anim_set_delay(&anim, 1000);
        anim_initialized = true;
    }
    return &anim;
}

//
// Event handlers
//

lv_obj_t *tick_value_change_obj;

//
// Screens
//

void create_screen_wifi() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.wifi = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    {
        lv_obj_t *parent_obj = obj;
        {
            // SSID
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.ssid = obj;
            lv_obj_set_pos(obj, 97, 65);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "SSID");
        }
        {
            // ssid_name
            lv_obj_t *obj = lv_textarea_create(parent_obj);
            objects.ssid_name = obj;
            lv_obj_set_pos(obj, 147, 61);
            lv_obj_set_size(obj, 150, 24);
            lv_textarea_set_max_length(obj, 128);
            lv_textarea_set_text(obj, "YeoSangMin_2G");
            lv_textarea_set_one_line(obj, false);
            lv_textarea_set_password_mode(obj, false);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE|LV_OBJ_FLAG_PRESS_LOCK|LV_OBJ_FLAG_SCROLL_CHAIN_HOR|LV_OBJ_FLAG_SCROLL_CHAIN_VER|LV_OBJ_FLAG_SCROLL_ELASTIC|LV_OBJ_FLAG_SCROLL_MOMENTUM|LV_OBJ_FLAG_SCROLL_ON_FOCUS|LV_OBJ_FLAG_SCROLL_WITH_ARROW|LV_OBJ_FLAG_SNAPPABLE);
            lv_obj_set_style_pad_top(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            // taget_ip
            lv_obj_t *obj = lv_textarea_create(parent_obj);
            objects.taget_ip = obj;
            lv_obj_set_pos(obj, 147, 102);
            lv_obj_set_size(obj, 150, 24);
            lv_textarea_set_max_length(obj, 128);
            lv_textarea_set_text(obj, "192.168.0.30");
            lv_textarea_set_one_line(obj, false);
            lv_textarea_set_password_mode(obj, false);
            lv_obj_set_style_pad_top(obj, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            // YTMD_IP
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.ytmd_ip = obj;
            lv_obj_set_pos(obj, 69, 106);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_label_set_text_static(obj, "YTMD_IP");
        }
        {
            // keboard
            lv_obj_t *obj = lv_keyboard_create(parent_obj);
            objects.keboard = obj;
            lv_obj_set_pos(obj, 31, 168);
            lv_obj_set_size(obj, 300, 120);
            lv_obj_set_style_align(obj, LV_ALIGN_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        {
            // connect_btn
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.connect_btn = obj;
            lv_obj_set_pos(obj, 110, 140);
            lv_obj_set_size(obj, 141, 28);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text_static(obj, "Button");
                }
            }
        }
        {
            // Saved Connections
            lv_obj_t *obj = lv_obj_create(parent_obj);
            objects.saved_connections = obj;
            lv_obj_set_pos(obj, 64, 180);
            lv_obj_set_size(obj, 233, 140);
        }
    }
    
    tick_screen_wifi();
}

void tick_screen_wifi() {
}

void create_screen_main() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.main = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 360, 360);
        }
        {
            // album_art
            lv_obj_t *obj = lv_img_create(parent_obj);
            objects.album_art = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 360, 360);
        }
        {
            // loading_spinner (boot / Wi-Fi / DHCP wait)
            lv_obj_t *obj = lv_spinner_create(parent_obj, 1000, 60);
            objects.loading_spinner = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 360, 360);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_color(obj, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_opa(obj, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        {
            // loadingbar (album-art download progress)
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.loadingbar = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 360, 360);
            lv_arc_set_range(obj, 0, 100);
            lv_arc_set_value(obj, 0);
            lv_arc_set_rotation(obj, 270);
            lv_arc_set_bg_angles(obj, 0, 360);
            lv_obj_remove_style(obj, NULL, LV_PART_KNOB);
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_arc_rounded(obj, false, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_rounded(obj, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    tick_screen_main();
}

void tick_screen_main() {
}

void create_screen_information() {
    lv_obj_t *obj = lv_obj_create(0);
    objects.information = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 360, 360);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_obj_create(parent_obj);
            lv_obj_set_pos(obj, 80, 42);
            lv_obj_set_size(obj, 200, 200);
            lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(obj, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_clip_corner(obj, true, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);

            // art2
            lv_obj_t *img = lv_img_create(obj);
            objects.art2 = img;
            lv_obj_set_pos(img, 0, 0);
            lv_obj_set_size(img, 200, 200);
        }
        {
            // seek
            lv_obj_t *obj = lv_arc_create(parent_obj);
            objects.seek = obj;
            lv_obj_set_pos(obj, 0, 0);
            lv_obj_set_size(obj, 360, 360);
            lv_arc_set_value(obj, 25);
            lv_obj_set_style_arc_rounded(obj, false, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_arc_rounded(obj, false, LV_PART_INDICATOR | LV_STATE_DEFAULT);
        }
        {
            // title
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.title = obj;
            lv_obj_set_pos(obj, 80, 242);
            lv_obj_set_size(obj, 200, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_font(obj, &ui_font_font_kor_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_anim(obj, get_anim(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_anim_time(obj, 50000, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_anim_speed(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "음악제목이 나오는 곳입니다~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
        }
        {
            // artist
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.artist = obj;
            lv_obj_set_pos(obj, 80, 273);
            lv_obj_set_size(obj, 200, LV_SIZE_CONTENT);
            lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_font(obj, &ui_font_font_kor_14, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_anim(obj, get_anim(), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_anim_time(obj, 50000, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_anim_speed(obj, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text_static(obj, "가수나오는 입니다~~~~~~~~~~~~~~~~~~~~~~~~~~~~~");
        }
    }
    
    tick_screen_information();
}

void tick_screen_information() {
}

typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_wifi,
    tick_screen_main,
    tick_screen_information,
};
void tick_screen(int screen_index) {
    if (screen_index >= 0 && screen_index < 3) {
        tick_screen_funcs[screen_index]();
    }
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen(screenId - 1);
}

//
// Fonts
//

ext_font_desc_t fonts[] = {
    { "Font_KOR_16", &ui_font_font_kor_16 },
    { "Font_KOR_14", &ui_font_font_kor_14 },
    { "Font_JP_16", &ui_font_font_jp_16 },
    { "Font_JP_14", &ui_font_font_jp_14 },
#if LV_FONT_MONTSERRAT_8
    { "MONTSERRAT_8", &lv_font_montserrat_8 },
#endif
#if LV_FONT_MONTSERRAT_10
    { "MONTSERRAT_10", &lv_font_montserrat_10 },
#endif
#if LV_FONT_MONTSERRAT_12
    { "MONTSERRAT_12", &lv_font_montserrat_12 },
#endif
#if LV_FONT_MONTSERRAT_14
    { "MONTSERRAT_14", &lv_font_montserrat_14 },
#endif
#if LV_FONT_MONTSERRAT_16
    { "MONTSERRAT_16", &lv_font_montserrat_16 },
#endif
#if LV_FONT_MONTSERRAT_18
    { "MONTSERRAT_18", &lv_font_montserrat_18 },
#endif
#if LV_FONT_MONTSERRAT_20
    { "MONTSERRAT_20", &lv_font_montserrat_20 },
#endif
#if LV_FONT_MONTSERRAT_22
    { "MONTSERRAT_22", &lv_font_montserrat_22 },
#endif
#if LV_FONT_MONTSERRAT_24
    { "MONTSERRAT_24", &lv_font_montserrat_24 },
#endif
#if LV_FONT_MONTSERRAT_26
    { "MONTSERRAT_26", &lv_font_montserrat_26 },
#endif
#if LV_FONT_MONTSERRAT_28
    { "MONTSERRAT_28", &lv_font_montserrat_28 },
#endif
#if LV_FONT_MONTSERRAT_30
    { "MONTSERRAT_30", &lv_font_montserrat_30 },
#endif
#if LV_FONT_MONTSERRAT_32
    { "MONTSERRAT_32", &lv_font_montserrat_32 },
#endif
#if LV_FONT_MONTSERRAT_34
    { "MONTSERRAT_34", &lv_font_montserrat_34 },
#endif
#if LV_FONT_MONTSERRAT_36
    { "MONTSERRAT_36", &lv_font_montserrat_36 },
#endif
#if LV_FONT_MONTSERRAT_38
    { "MONTSERRAT_38", &lv_font_montserrat_38 },
#endif
#if LV_FONT_MONTSERRAT_40
    { "MONTSERRAT_40", &lv_font_montserrat_40 },
#endif
#if LV_FONT_MONTSERRAT_42
    { "MONTSERRAT_42", &lv_font_montserrat_42 },
#endif
#if LV_FONT_MONTSERRAT_44
    { "MONTSERRAT_44", &lv_font_montserrat_44 },
#endif
#if LV_FONT_MONTSERRAT_46
    { "MONTSERRAT_46", &lv_font_montserrat_46 },
#endif
#if LV_FONT_MONTSERRAT_48
    { "MONTSERRAT_48", &lv_font_montserrat_48 },
#endif
};

//
// Color themes
//

uint32_t active_theme_index = 0;

//
//
//

void create_screens() {

// Set default LVGL theme
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    // Initialize screens
    // Create screens
    create_screen_wifi();
    create_screen_main();
    create_screen_information();
}
