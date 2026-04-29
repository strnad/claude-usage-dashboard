/*
 * Dashboard UI — LVGL 8.4 implementation.
 */

#include "ui_dashboard.h"
#include "app_display.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "lvgl.h"
#include "esp_log.h"

extern const lv_img_dsc_t claude_logo;

static const char *TAG = "ui";

/* Color palette */
#define CLR_BG          lv_color_hex(0x0A0A0A)
#define CLR_PANEL       lv_color_hex(0x141414)
#define CLR_BORDER      lv_color_hex(0x2A2A2A)
#define CLR_TEXT        lv_color_hex(0xE5E5E5)
#define CLR_TEXT_DIM    lv_color_hex(0x8A8A8A)
#define CLR_TEXT_MUTE   lv_color_hex(0x4D4D4D)
#define CLR_ACCENT      lv_color_hex(0xA78BFA)  /* Claude purple */
#define CLR_GREEN       lv_color_hex(0x4ADE80)
#define CLR_YELLOW      lv_color_hex(0xFACC15)
#define CLR_ORANGE      lv_color_hex(0xFB923C)
#define CLR_RED         lv_color_hex(0xEF4444)
#define CLR_DOT_OFF     lv_color_hex(0x404040)
#define CLR_DOT_ON      lv_color_hex(0xA78BFA)

#define MAX_DOTS 8

/* Roots */
static lv_obj_t *s_root        = NULL;  /* dashboard layout */
static lv_obj_t *s_overlay     = NULL;  /* connecting/error overlay */
static lv_obj_t *s_overlay_lbl = NULL;
static lv_obj_t *s_overlay_sub = NULL;

static lv_obj_t *s_long_overlay = NULL;
static lv_obj_t *s_long_lbl     = NULL;

/* Header */
static lv_obj_t *s_img_logo    = NULL;
static lv_obj_t *s_lbl_label   = NULL;
static lv_obj_t *s_lbl_email   = NULL;
static lv_obj_t *s_lbl_ip      = NULL;
static lv_obj_t *s_lbl_wifi    = NULL;

/* 5h block */
static lv_obj_t *s_lbl_5h_title  = NULL;
static lv_obj_t *s_lbl_5h_pct    = NULL;
static lv_obj_t *s_lbl_5h_reset  = NULL;
static lv_obj_t *s_bar_5h_bg     = NULL;
static lv_obj_t *s_bar_5h_fill   = NULL;

/* 7d block */
static lv_obj_t *s_lbl_7d_title  = NULL;
static lv_obj_t *s_lbl_7d_pct    = NULL;
static lv_obj_t *s_lbl_7d_reset  = NULL;
static lv_obj_t *s_bar_7d_bg     = NULL;
static lv_obj_t *s_bar_7d_fill   = NULL;

/* Footer */
static lv_obj_t *s_dots_box     = NULL;
static lv_obj_t *s_dot_objs[MAX_DOTS] = {0};
static lv_obj_t *s_lbl_clock    = NULL;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static lv_color_t color_for_pct(float p)
{
    if (p >= 95.0f) return CLR_RED;
    if (p >= 80.0f) return CLR_ORANGE;
    if (p >= 50.0f) return CLR_YELLOW;
    return CLR_GREEN;
}

static void format_eta(int64_t resets_at_ms, char *out, size_t n)
{
    if (resets_at_ms <= 0) { snprintf(out, n, "—"); return; }
    struct timeval tv; gettimeofday(&tv, NULL);
    int64_t now_ms = (int64_t)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
    if (now_ms < 1735689600000LL) { snprintf(out, n, "—"); return; }
    int64_t diff_s = (resets_at_ms - now_ms) / 1000LL;
    if (diff_s <= 0) { snprintf(out, n, "any time"); return; }
    int days = (int)(diff_s / 86400);
    int hours = (int)((diff_s % 86400) / 3600);
    int mins = (int)((diff_s % 3600) / 60);
    if (days > 0) {
        snprintf(out, n, "reset in %dd %dh", days, hours);
    } else if (hours > 0) {
        snprintf(out, n, "reset in %dh %dm", hours, mins);
    } else {
        snprintf(out, n, "reset in %dm", mins);
    }
}

static lv_obj_t *make_bar_bg(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, 304, 12);
    lv_obj_set_pos(bg, 8, y);
    lv_obj_set_style_radius(bg, 6, 0);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x1F1F1F), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    return bg;
}

static lv_obj_t *make_bar_fill(lv_obj_t *bg)
{
    lv_obj_t *fill = lv_obj_create(bg);
    lv_obj_remove_style_all(fill);
    lv_obj_set_size(fill, 0, 12);
    lv_obj_set_pos(fill, 0, 0);
    lv_obj_set_style_radius(fill, 6, 0);
    lv_obj_set_style_bg_color(fill, CLR_GREEN, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fill, 0, 0);
    lv_obj_clear_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    return fill;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                             lv_coord_t x, lv_coord_t y, const char *txt)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_pos(lbl, x, y);
    lv_label_set_text(lbl, txt);
    return lbl;
}

/* ------------------------------------------------------------------ */
/* Build                                                               */
/* ------------------------------------------------------------------ */

void ui_dashboard_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* Root container = full screen */
    s_root = lv_obj_create(scr);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, 320, 172);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, CLR_BG, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    /* Header strip 0..24 */
    s_img_logo = lv_img_create(s_root);
    lv_img_set_src(s_img_logo, &claude_logo);
    lv_obj_set_pos(s_img_logo, 6, 4);
    s_lbl_label = make_label(s_root, &lv_font_montserrat_16, CLR_TEXT, 36, 4, "—");
    s_lbl_email = make_label(s_root, &lv_font_montserrat_12, CLR_TEXT_DIM, 36, 22, "");
    lv_obj_set_width(s_lbl_email, 160);
    lv_label_set_long_mode(s_lbl_email, LV_LABEL_LONG_DOT);
    s_lbl_ip = make_label(s_root, &lv_font_montserrat_12, CLR_TEXT_DIM, 170, 22, "");
    lv_obj_set_width(s_lbl_ip, 138);
    lv_obj_set_style_text_align(s_lbl_ip, LV_TEXT_ALIGN_RIGHT, 0);
    /* Hide email by default if blank */
    s_lbl_wifi  = make_label(s_root, &lv_font_montserrat_12, CLR_TEXT_DIM, 280, 6, LV_SYMBOL_WIFI);

    /* Divider after header */
    lv_obj_t *div1 = lv_obj_create(s_root);
    lv_obj_remove_style_all(div1);
    lv_obj_set_size(div1, 304, 1);
    lv_obj_set_pos(div1, 8, 38);
    lv_obj_set_style_bg_color(div1, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(div1, LV_OPA_COVER, 0);

    /* 5h block — y=42 */
    s_lbl_5h_title = make_label(s_root, &lv_font_montserrat_12, CLR_TEXT_DIM, 8, 42, "5H LIMIT");
    s_lbl_5h_pct   = make_label(s_root, &lv_font_montserrat_24, CLR_TEXT, 8, 56, "—%");
    s_lbl_5h_reset = make_label(s_root, &lv_font_montserrat_12, CLR_TEXT_DIM, 8, 88, "—");
    /* Right-align reset label by setting width */
    lv_label_set_long_mode(s_lbl_5h_reset, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_lbl_5h_reset, 304);
    lv_obj_set_style_text_align(s_lbl_5h_reset, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_lbl_5h_reset, 8, 64);
    s_bar_5h_bg   = make_bar_bg(s_root, 86);
    s_bar_5h_fill = make_bar_fill(s_bar_5h_bg);

    /* 7d block — y=102 */
    s_lbl_7d_title = make_label(s_root, &lv_font_montserrat_12, CLR_TEXT_DIM, 8, 102, "7D LIMIT");
    s_lbl_7d_pct   = make_label(s_root, &lv_font_montserrat_24, CLR_TEXT, 8, 116, "—%");
    s_lbl_7d_reset = make_label(s_root, &lv_font_montserrat_12, CLR_TEXT_DIM, 8, 148, "—");
    lv_label_set_long_mode(s_lbl_7d_reset, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_lbl_7d_reset, 304);
    lv_obj_set_style_text_align(s_lbl_7d_reset, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_lbl_7d_reset, 8, 124);
    s_bar_7d_bg   = make_bar_bg(s_root, 146);
    s_bar_7d_fill = make_bar_fill(s_bar_7d_bg);

    /* Footer — y=160 */
    s_dots_box = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_dots_box);
    lv_obj_set_size(s_dots_box, 200, 10);
    lv_obj_set_pos(s_dots_box, 8, 162);
    lv_obj_set_style_bg_opa(s_dots_box, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_dots_box, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < MAX_DOTS; i++) {
        lv_obj_t *d = lv_obj_create(s_dots_box);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 6, 6);
        lv_obj_set_pos(d, 4 + i * 12, 2);
        lv_obj_set_style_radius(d, 3, 0);
        lv_obj_set_style_bg_color(d, CLR_DOT_OFF, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
        s_dot_objs[i] = d;
    }

    s_lbl_clock = make_label(s_root, &lv_font_montserrat_12, CLR_TEXT_DIM, 270, 159, "--:--");
    lv_obj_set_width(s_lbl_clock, 44);
    lv_obj_set_style_text_align(s_lbl_clock, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_lbl_clock, 264, 160);

    ESP_LOGI(TAG, "Dashboard UI built");
}

/* ------------------------------------------------------------------ */
/* Update                                                              */
/* ------------------------------------------------------------------ */

static void update_bar(lv_obj_t *fill, float pct)
{
    if (!fill) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int w = (int)(304.0f * pct / 100.0f);
    if (w < 0) w = 0;
    if (w > 304) w = 304;
    lv_obj_set_width(fill, w);
    lv_obj_set_style_bg_color(fill, color_for_pct(pct), 0);
}

static void render_clock(void)
{
    if (!s_lbl_clock) return;
    time_t t = time(NULL);
    if (t < 1735689600) {
        lv_label_set_text(s_lbl_clock, "--:--");
        return;
    }
    struct tm tm; localtime_r(&t, &tm);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(s_lbl_clock, buf);
}

void ui_dashboard_update(uint8_t account_idx, uint8_t total_accounts,
                        const char *label, const char *email, const char *tier,
                        const claude_usage_t *data, bool wifi_ok)
{
    if (s_lbl_label) {
        char buf[APP_LABEL_MAX_LEN + APP_TIER_MAX_LEN + 4];
        const char *l = (label && label[0]) ? label : "(unnamed)";
        if (tier && tier[0]) snprintf(buf, sizeof(buf), "%s (%s)", l, tier);
        else                  snprintf(buf, sizeof(buf), "%s", l);
        lv_label_set_text(s_lbl_label, buf);
    }
    if (s_lbl_email) {
        if (email && email[0]) {
            lv_label_set_text(s_lbl_email, email);
            lv_obj_clear_flag(s_lbl_email, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_lbl_email, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_lbl_wifi) {
        lv_obj_set_style_text_color(s_lbl_wifi, wifi_ok ? CLR_GREEN : CLR_RED, 0);
    }

    char buf[32];

    if (data && data->valid) {
        /* 5h */
        if (data->five_h_util >= 0) {
            snprintf(buf, sizeof(buf), "%d%%", (int)(data->five_h_util + 0.5f));
            lv_label_set_text(s_lbl_5h_pct, buf);
            lv_obj_set_style_text_color(s_lbl_5h_pct, color_for_pct(data->five_h_util), 0);
            update_bar(s_bar_5h_fill, data->five_h_util);
        } else {
            lv_label_set_text(s_lbl_5h_pct, "—");
            update_bar(s_bar_5h_fill, 0);
        }
        format_eta(data->five_h_resets_at_ms, buf, sizeof(buf));
        lv_label_set_text(s_lbl_5h_reset, buf);

        /* 7d */
        if (data->seven_d_util >= 0) {
            snprintf(buf, sizeof(buf), "%d%%", (int)(data->seven_d_util + 0.5f));
            lv_label_set_text(s_lbl_7d_pct, buf);
            lv_obj_set_style_text_color(s_lbl_7d_pct, color_for_pct(data->seven_d_util), 0);
            update_bar(s_bar_7d_fill, data->seven_d_util);
        } else {
            lv_label_set_text(s_lbl_7d_pct, "—");
            update_bar(s_bar_7d_fill, 0);
        }
        format_eta(data->seven_d_resets_at_ms, buf, sizeof(buf));
        lv_label_set_text(s_lbl_7d_reset, buf);
    } else {
        lv_label_set_text(s_lbl_5h_pct, "—");
        lv_label_set_text(s_lbl_5h_reset, data && data->error_msg[0] ? data->error_msg : "no data");
        update_bar(s_bar_5h_fill, 0);

        lv_label_set_text(s_lbl_7d_pct, "—");
        lv_label_set_text(s_lbl_7d_reset, "");
        update_bar(s_bar_7d_fill, 0);
    }

    /* Page dots */
    for (int i = 0; i < MAX_DOTS; i++) {
        if (!s_dot_objs[i]) continue;
        if (i < total_accounts) {
            lv_obj_clear_flag(s_dot_objs[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_dot_objs[i], (i == account_idx) ? CLR_DOT_ON : CLR_DOT_OFF, 0);
        } else {
            lv_obj_add_flag(s_dot_objs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    render_clock();
}

/* ------------------------------------------------------------------ */
/* Overlays                                                            */
/* ------------------------------------------------------------------ */

static void ensure_overlay(void)
{
    if (s_overlay) return;
    s_overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, 320, 172);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_bg_color(s_overlay, CLR_BG, 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_overlay_lbl = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_overlay_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_overlay_lbl, CLR_ACCENT, 0);
    lv_label_set_text(s_overlay_lbl, "Claude Dashboard");
    lv_obj_align(s_overlay_lbl, LV_ALIGN_CENTER, 0, -20);

    s_overlay_sub = lv_label_create(s_overlay);
    lv_obj_set_style_text_font(s_overlay_sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_overlay_sub, CLR_TEXT_DIM, 0);
    lv_label_set_text(s_overlay_sub, "");
    lv_obj_set_width(s_overlay_sub, 300);
    lv_obj_set_style_text_align(s_overlay_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_overlay_sub, LV_ALIGN_CENTER, 0, 16);
}

void ui_dashboard_show_connecting(const char *ssid)
{
    ensure_overlay();
    char buf[64];
    snprintf(buf, sizeof(buf), "Connecting to %s ...", ssid ? ssid : "");
    lv_label_set_text(s_overlay_lbl, "Claude Dashboard");
    lv_label_set_text(s_overlay_sub, buf);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_dashboard_show_error(const char *msg)
{
    ensure_overlay();
    lv_label_set_text(s_overlay_lbl, "Error");
    lv_label_set_text(s_overlay_sub, msg ? msg : "unknown");
    lv_obj_set_style_text_color(s_overlay_lbl, CLR_RED, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_dashboard_show_info(const char *title, const char *msg)
{
    ensure_overlay();
    lv_label_set_text(s_overlay_lbl, title ? title : "Info");
    lv_label_set_text(s_overlay_sub, msg ? msg : "");
    lv_obj_set_style_text_color(s_overlay_lbl, lv_color_hex(0xD97757), 0);  /* Claude orange */
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_dashboard_hide_overlay(void)
{
    if (s_overlay) lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* Long-press progress overlay (semi-transparent) */
void ui_dashboard_show_long_press_msg(const char *msg, uint8_t secs_remaining)
{
    if (!s_long_overlay) {
        s_long_overlay = lv_obj_create(lv_scr_act());
        lv_obj_remove_style_all(s_long_overlay);
        lv_obj_set_size(s_long_overlay, 320, 172);
        lv_obj_set_pos(s_long_overlay, 0, 0);
        lv_obj_set_style_bg_color(s_long_overlay, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_long_overlay, LV_OPA_70, 0);
        lv_obj_clear_flag(s_long_overlay, LV_OBJ_FLAG_SCROLLABLE);

        s_long_lbl = lv_label_create(s_long_overlay);
        lv_obj_set_style_text_font(s_long_lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(s_long_lbl, CLR_RED, 0);
        lv_obj_set_width(s_long_lbl, 300);
        lv_obj_set_style_text_align(s_long_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_long_lbl, LV_ALIGN_CENTER, 0, 0);
    }
    char b[64];
    snprintf(b, sizeof(b), "%s\n%us", msg ? msg : "Hold", (unsigned)secs_remaining);
    lv_label_set_text(s_long_lbl, b);
    lv_obj_clear_flag(s_long_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_dashboard_hide_long_press(void)
{
    if (s_long_overlay) lv_obj_add_flag(s_long_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_dashboard_set_ip(const char *ip)
{
    if (!s_lbl_ip) return;
    if (ip && ip[0]) {
        lv_label_set_text(s_lbl_ip, ip);
        lv_obj_clear_flag(s_lbl_ip, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lbl_ip, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Compatibility wrapper — defaults to "Hold to toggle cycle" message */
void ui_dashboard_show_long_press(uint8_t secs_remaining)
{
    ui_dashboard_show_long_press_msg("Hold to toggle cycle", secs_remaining);
}
