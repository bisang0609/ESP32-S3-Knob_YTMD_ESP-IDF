#include "app_fonts.h"

#include "lvgl.h"
#include "ui/screens.h"

LV_FONT_DECLARE(Font_UI_16);
LV_FONT_DECLARE(Font_UI_20);

void app_init_fonts(void)
{
    if (objects.title) {
        lv_obj_set_style_text_font(objects.title, &Font_UI_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(objects.title, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(objects.title, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_invalidate(objects.title);
    }

    if (objects.artist) {
        lv_obj_set_style_text_font(objects.artist, &Font_UI_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(objects.artist, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(objects.artist, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_invalidate(objects.artist);
    }
}
