/*
 * Setup-mode UI.
 */

#include "ui_setup.h"
#include "app_display.h"

#include "lvgl.h"
#include <stdio.h>

#define CLR_BG         lv_color_hex(0x0A0A0A)
#define CLR_TEXT       lv_color_hex(0xE5E5E5)
#define CLR_TEXT_DIM   lv_color_hex(0x8A8A8A)
#define CLR_ACCENT     lv_color_hex(0xA78BFA)

void ui_setup_show(const char *ap_ssid)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, CLR_ACCENT, 0);
    lv_label_set_text(title, "Setup mode");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *line1 = lv_label_create(scr);
    lv_obj_set_style_text_font(line1, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(line1, CLR_TEXT_DIM, 0);
    lv_label_set_text(line1, "Join WiFi:");
    lv_obj_align(line1, LV_ALIGN_TOP_LEFT, 12, 50);

    lv_obj_t *ssid_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(ssid_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ssid_lbl, CLR_TEXT, 0);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", ap_ssid ? ap_ssid : "Claude-Dashboard");
    lv_label_set_text(ssid_lbl, buf);
    lv_obj_align(ssid_lbl, LV_ALIGN_TOP_LEFT, 12, 70);

    lv_obj_t *line2 = lv_label_create(scr);
    lv_obj_set_style_text_font(line2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(line2, CLR_TEXT_DIM, 0);
    lv_label_set_text(line2, "Open in browser:");
    lv_obj_align(line2, LV_ALIGN_TOP_LEFT, 12, 100);

    lv_obj_t *url_lbl = lv_label_create(scr);
    lv_obj_set_style_text_font(url_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(url_lbl, CLR_TEXT, 0);
    lv_label_set_text(url_lbl, "http://192.168.4.1");
    lv_obj_align(url_lbl, LV_ALIGN_TOP_LEFT, 12, 120);

    lv_obj_t *footer = lv_label_create(scr);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(footer, CLR_TEXT_DIM, 0);
    lv_label_set_text(footer, "AP is open (no password)");
    lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 12, -8);
}
