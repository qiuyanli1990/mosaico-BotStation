#include "ui.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_state.h"
#include "bsp/esp_mosaico.h"
#include "bot_station.h"
#include "device_power.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lvgl.h"
#include "media.h"
#include "network.h"
#include "session.h"

#define TAG "APP_UI"

enum {
    COLOR_BG = 0xFFF8EF,
    COLOR_BG_END = 0xFBEBDD,
    COLOR_PANEL = 0xFFFCF7,
    COLOR_RAISED = 0xF5E4D8,
    COLOR_LINE = 0xE8D5C6,
    COLOR_TEXT = 0x514347,
    COLOR_MUTED = 0x77605B,
    COLOR_CYAN = 0x216F78,
    COLOR_GREEN = 0x2E7054,
    COLOR_YELLOW = 0x845B15,
    COLOR_ORANGE = 0x9D4E26,
    COLOR_RED = 0xA73D56,
    COLOR_MINT = 0xE1EFE2,
    COLOR_PEACH = 0xF8DFCE,
    COLOR_PINK = 0xF7DDE2,
    COLOR_BUTTER = 0xF7E9BC,
    COLOR_SKY = 0xDFEEF0,
    COLOR_DOT = 0xEFDCCB,
    COLOR_SCRIM = 0x77635E,
};

typedef enum {
    BULB_STOPPED,
    BULB_CONNECTING,
    BULB_LISTENING,
    BULB_THINKING,
    BULB_SPEAKING,
    BULB_ERROR,
} bulb_state_t;

LV_FONT_DECLARE(pair_code_font);

static lv_obj_t *pair_code_label;
static lv_obj_t *action;
static lv_obj_t *action_label;
static lv_obj_t *wifi_status_button;
static lv_obj_t *wifi_connection_label;
static lv_obj_t *wifi_ssid_label;
static lv_obj_t *conversation_indicator;
static lv_obj_t *conversation_status_label;
static lv_obj_t *conversation_hint;
static lv_obj_t *hero_heading;
static lv_obj_t *clock_label;
static lv_obj_t *seconds_label;
static lv_obj_t *date_label;
static lv_obj_t *clock_hint;
static lv_obj_t *duration_label;
static lv_obj_t *duration_heading;
static lv_obj_t *link_label;
static lv_obj_t *link_heading;
static lv_obj_t *battery_label;
static lv_obj_t *battery_status_label;
static lv_obj_t *brightness_button;
static lv_obj_t *brightness_button_label;
static lv_obj_t *brightness_sheet;
static lv_obj_t *brightness_slider;
static lv_obj_t *brightness_value;
static lv_obj_t *brightness_status;
static lv_obj_t *details_button;
static lv_obj_t *details_page;
static lv_obj_t *details_status;
static lv_obj_t *user_message;
static lv_obj_t *assistant_message;
static lv_obj_t *volume_slider;
static lv_obj_t *volume_label;
static lv_obj_t *mute_label;
static lv_obj_t *wifi_page;
static lv_obj_t *password_page;
static lv_obj_t *wifi_status_label;
static lv_obj_t *password_status_label;
static lv_obj_t *scan_button;
static lv_obj_t *scan_button_label;
static lv_obj_t *ap_list;
static lv_obj_t *selected_label;
static lv_obj_t *password_input;
static network_ap_t displayed_aps[NETWORK_MAX_APS];
static network_ap_t selected_ap;
static uint32_t displayed_generation;
static bool pair_code_mode = true;
static bool wifi_connect_pending;
static bulb_state_t bulb_state = BULB_STOPPED;
static unsigned bulb_color = COLOR_MUTED;
static unsigned unmuted_volume = MEDIA_DEFAULT_SPEAKER_VOLUME;
static uint32_t session_started_ms;
static uint32_t session_elapsed_seconds;
static bool session_clock_running;
static atomic_bool ai_button_pending;
static lv_style_t wifi_row_style;
static lv_style_t wifi_row_pressed_style;
static unsigned brightness_target = DEVICE_BRIGHTNESS_DEFAULT;
static bool brightness_pending;
static uint32_t brightness_applied_ms;

static void start_stop(lv_event_t *event);

static void set_visible(lv_obj_t *object, bool visible)
{
    if (visible) {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_label_text(lv_obj_t *object, const char *text)
{
    if (strcmp(lv_label_get_text(object), text) != 0) {
        lv_label_set_text(object, text);
    }
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       const lv_font_t *font, unsigned color)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    return object;
}

static lv_obj_t *centered_label(lv_obj_t *parent, const char *text, int y,
                                 const lv_font_t *font, unsigned color)
{
    lv_obj_t *object = label(parent, text, 24, y, font, color);
    lv_obj_set_width(object, 432);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_DOTS);
    return object;
}

static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int width, int height,
                       unsigned color, int radius)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(object, radius > 0 ? 1 : 0, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y,
                        int width, int height, unsigned color)
{
    lv_obj_t *object = lv_button_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(object, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(COLOR_RAISED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(object, lv_color_hex(COLOR_LINE), LV_STATE_DISABLED);
    lv_obj_set_style_transform_width(object, 0, LV_STATE_PRESSED);
    lv_obj_set_style_transform_height(object, 0, LV_STATE_PRESSED);
    lv_obj_t *caption = lv_label_create(object);
    lv_label_set_text(caption, text);
    lv_obj_center(caption);
    return object;
}

static void refresh_power(void)
{
    device_power_status_t power;
    device_power_get_status(&power);
    char text[64];
    unsigned color = COLOR_MUTED;
    const char *status = "";
    if (power.battery == DEVICE_BATTERY_AVAILABLE) {
        const char *icon = power.battery_percent >= 90 ? LV_SYMBOL_BATTERY_FULL :
                           power.battery_percent >= 65 ? LV_SYMBOL_BATTERY_3 :
                           power.battery_percent >= 35 ? LV_SYMBOL_BATTERY_2 :
                           power.battery_percent >= 15 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        snprintf(text, sizeof(text), "%s  %u%%", icon, power.battery_percent);
        status = power.charging ? LV_SYMBOL_CHARGE " Charging" : power.full ? "Full" : "";
        color = power.charging || power.full ? COLOR_GREEN :
                power.battery_percent <= 20 ? COLOR_RED : COLOR_TEXT;
    } else {
        snprintf(text, sizeof(text), "%s  --", LV_SYMBOL_BATTERY_EMPTY);
        status = power.battery == DEVICE_BATTERY_ABSENT ? "No battery" : "Battery unavailable";
    }
    set_label_text(battery_label, text);
    set_label_text(battery_status_label, status);
    set_visible(battery_status_label, status[0] != '\0');
    static unsigned previous_color;
    if (previous_color != color) {
        previous_color = color;
        lv_obj_set_style_text_color(battery_label, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(battery_status_label, lv_color_hex(color), 0);
    }
    if (power.brightness_available) {
        snprintf(text, sizeof(text), "%u%%", power.brightness);
    } else {
        strcpy(text, "--");
    }
    set_label_text(brightness_button_label, text);
    if (!brightness_pending && !lv_obj_has_state(brightness_slider, LV_STATE_PRESSED)) {
        brightness_target = power.brightness;
        if ((unsigned)lv_slider_get_value(brightness_slider) != power.brightness) {
            lv_slider_set_value(brightness_slider, power.brightness, LV_ANIM_OFF);
        }
        snprintf(text, sizeof(text), "%u%%", power.brightness);
        set_label_text(brightness_value, text);
    }
}

static void apply_brightness(void)
{
    if (!brightness_pending) {
        return;
    }
    brightness_pending = false;
    brightness_applied_ms = lv_tick_get();
    esp_err_t result = device_power_set_brightness(brightness_target);
    set_label_text(brightness_status, result == ESP_OK ? "Find your comfortable light" :
                   "Could not adjust brightness. Try again.");
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Brightness change failed: %s", esp_err_to_name(result));
    }
    refresh_power();
}

static void brightness_changed(lv_event_t *event)
{
    brightness_target = (unsigned)lv_slider_get_value(brightness_slider);
    brightness_pending = true;
    char text[16];
    snprintf(text, sizeof(text), "%u%%", brightness_target);
    set_label_text(brightness_value, text);
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        apply_brightness();
    }
}

static void brightness_preset(lv_event_t *event)
{
    brightness_target = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    brightness_pending = true;
    lv_slider_set_value(brightness_slider, brightness_target, LV_ANIM_OFF);
    apply_brightness();
}

static void open_brightness(lv_event_t *event)
{
    (void)event;
    refresh_power();
    set_label_text(brightness_status, "Find your comfortable light");
    lv_obj_remove_flag(brightness_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void close_brightness(lv_event_t *event)
{
    (void)event;
    apply_brightness();
    lv_obj_add_flag(brightness_sheet, LV_OBJ_FLAG_HIDDEN);
}

static void brightness_scrim_clicked(lv_event_t *event)
{
    if (lv_event_get_target_obj(event) == brightness_sheet) {
        close_brightness(event);
    }
}

static void draw_sun(lv_event_t *event)
{
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    const int x = bounds.x1 + 22;
    const int y = (bounds.y1 + bounds.y2 + 1) / 2;
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = (lv_point_t){x, y};
    arc.radius = 7;
    arc.width = 2;
    arc.color = lv_color_hex(COLOR_ORANGE);
    arc.start_angle = 0;
    arc.end_angle = 360;
    lv_draw_arc(layer, &arc);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = arc.color;
    line.width = 2;
    static const int8_t rays[][4] = {
        {0, -11, 0, -15}, {0, 11, 0, 15}, {-11, 0, -15, 0}, {11, 0, 15, 0},
        {-8, -8, -11, -11}, {8, -8, 11, -11}, {-8, 8, -11, 11}, {8, 8, 11, 11},
    };
    for (size_t i = 0; i < sizeof(rays) / sizeof(rays[0]); ++i) {
        line.p1 = (lv_point_precise_t){x + rays[i][0], y + rays[i][1]};
        line.p2 = (lv_point_precise_t){x + rays[i][2], y + rays[i][3]};
        lv_draw_line(layer, &line);
    }
}

static void open_details(lv_event_t *event)
{
    (void)event;
    lv_obj_remove_flag(details_page, LV_OBJ_FLAG_HIDDEN);
}

static void close_details(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(details_page, LV_OBJ_FLAG_HIDDEN);
}

static void draw_disc(lv_layer_t *layer, int x, int y, int radius, unsigned color)
{
    lv_draw_rect_dsc_t disc;
    lv_draw_rect_dsc_init(&disc);
    disc.radius = LV_RADIUS_CIRCLE;
    disc.bg_color = lv_color_hex(color);
    lv_area_t area = {x - radius, y - radius, x + radius, y + radius};
    lv_draw_rect(layer, &disc, &area);
}

static void draw_sparkle(lv_layer_t *layer, int x, int y, int radius, unsigned color)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(color);
    line.width = 2;
    line.round_start = true;
    line.round_end = true;
    line.p1 = (lv_point_precise_t){x - radius, y};
    line.p2 = (lv_point_precise_t){x + radius, y};
    lv_draw_line(layer, &line);
    line.p1 = (lv_point_precise_t){x, y - radius};
    line.p2 = (lv_point_precise_t){x, y + radius};
    lv_draw_line(layer, &line);
}

static void draw_cloud(lv_layer_t *layer, int x, int y)
{
    draw_disc(layer, x + 15, y + 16, 14, COLOR_PANEL);
    draw_disc(layer, x + 35, y + 9, 20, COLOR_PANEL);
    draw_disc(layer, x + 56, y + 17, 13, COLOR_PANEL);
    lv_draw_rect_dsc_t base;
    lv_draw_rect_dsc_init(&base);
    base.radius = 12;
    base.bg_color = lv_color_hex(COLOR_PANEL);
    lv_area_t area = {x + 9, y + 14, x + 61, y + 30};
    lv_draw_rect(layer, &base, &area);
}

static void draw_background(lv_event_t *event)
{
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    const int x = bounds.x1;
    const int y = bounds.y1;

    /* Decorations share the partial renderer; no bitmap or extra widget pool. */
    draw_disc(layer, x - 30, y + 280, 90, COLOR_PEACH);
    draw_disc(layer, x + 499, y + 174, 70, COLOR_MINT);
    for (int row = 0; row < 8; ++row) {
        for (int column = 0; column < 11; ++column) {
            draw_disc(layer, x + 32 + column * 40 + (row % 2) * 12,
                       y + 142 + row * 36, 1, COLOR_DOT);
        }
    }
    draw_cloud(layer, x + 30, y + 157);
    draw_cloud(layer, x + 374, y + 287);
    draw_sparkle(layer, x + 113, y + 185, 5, COLOR_PEACH);
    draw_sparkle(layer, x + 370, y + 160, 6, COLOR_BUTTER);
    draw_sparkle(layer, x + 109, y + 314, 4, COLOR_ORANGE);
}

static void style_page(lv_obj_t *page)
{
    lv_obj_set_style_bg_color(page, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_grad_color(page, lv_color_hex(COLOR_BG_END), 0);
    lv_obj_set_style_bg_grad_dir(page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_color(page, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(page, draw_background, LV_EVENT_DRAW_MAIN, NULL);
}

static void refresh_clock(void)
{
    static time_t last_second = (time_t)-1;
    const time_t now = time(NULL);
    if (now == last_second) {
        return;
    }
    last_second = now;
    if (!network_time_ready()) {
        set_label_text(clock_label, "--:--");
        set_label_text(seconds_label, "--");
        set_label_text(date_label, "Date pending");
        set_label_text(clock_hint, "BEIJING / UTC+8");
        return;
    }
    /* Apply the display offset locally; TLS/JWT and SNTP keep their UTC clock. */
    time_t beijing = now + 8 * 60 * 60;
    struct tm local;
    gmtime_r(&beijing, &local);
    char text[40];
    strftime(text, sizeof(text), "%H:%M", &local);
    set_label_text(clock_label, text);
    strftime(text, sizeof(text), "%S", &local);
    set_label_text(seconds_label, text);
    strftime(text, sizeof(text), "%a, %d %b %Y", &local);
    set_label_text(date_label, text);
    set_label_text(clock_hint, "BEIJING / UTC+8");
}

static void refresh_session_info(const app_snapshot_t *state)
{
    if (state->phase == APP_RUNNING) {
        if (!session_clock_running) {
            session_started_ms = lv_tick_get();
            session_clock_running = true;
        }
        session_elapsed_seconds = lv_tick_elaps(session_started_ms) / 1000;
    } else {
        session_clock_running = false;
        if (state->phase == APP_STARTING || state->phase == APP_CONNECTING) {
            session_elapsed_seconds = 0;
        }
    }
    char text[32];
    snprintf(text, sizeof(text), "%02u:%02u", (unsigned)(session_elapsed_seconds / 60),
             (unsigned)(session_elapsed_seconds % 60));
    set_label_text(duration_label, text);
    set_label_text(link_label, state->phase == APP_RUNNING ? "LIVE" : "IDLE");
    set_label_text(details_status, state->message);
    set_label_text(user_message, state->user_transcript[0] ? state->user_transcript :
                   "Your latest message will appear here.");
    set_label_text(assistant_message, state->assistant_transcript[0] ?
                   state->assistant_transcript : "Your assistant's latest reply will appear here.");
}

static void show_wifi_page(bool scan)
{
    close_brightness(NULL);
    lv_obj_add_flag(details_page, LV_OBJ_FLAG_HIDDEN);
    lv_textarea_set_text(password_input, "");
    lv_obj_add_flag(password_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(wifi_page, LV_OBJ_FLAG_HIDDEN);
    if (scan) {
        network_scan_start();
    }
}

static void open_wifi(lv_event_t *event)
{
    (void)event;
    show_wifi_page(true);
}

static void close_wifi(lv_event_t *event)
{
    (void)event;
    lv_obj_add_flag(wifi_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(password_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(ap_list);
    displayed_generation = UINT32_MAX;
}

static void update_wifi_status_button(const network_status_t *network)
{
    static int previous_connection = -1;
    const bool connected = network->connection == NETWORK_CONNECTED;
    const bool connecting = network->connection == NETWORK_CONNECTING;
    unsigned foreground = connected ? COLOR_GREEN :
                          connecting ? COLOR_YELLOW : COLOR_RED;
    unsigned background = connected ? COLOR_MINT :
                          connecting ? COLOR_BUTTER : COLOR_PINK;
    if (previous_connection != (int)network->connection) {
        previous_connection = network->connection;
        lv_obj_set_style_text_color(wifi_status_button, lv_color_hex(foreground), 0);
        lv_obj_set_style_bg_color(wifi_status_button, lv_color_hex(background), 0);
        lv_obj_set_style_text_color(wifi_connection_label, lv_color_hex(foreground), 0);
    }
    const char *connection = connected ? "WI-FI ONLINE" :
                             connecting ? "CONNECTING..." :
                             network->connection == NETWORK_FAILED ?
                             "WI-FI OFFLINE" : "SET UP WI-FI";
    set_label_text(wifi_connection_label, connection);
    set_label_text(wifi_ssid_label,
                   network->ssid[0] ? network->ssid : "Tap to connect");
}

static void refresh_volume(void)
{
    static int previous_volume = -1;
    unsigned value = media_get_volume();
    if ((unsigned)lv_slider_get_value(volume_slider) != value) {
        lv_slider_set_value(volume_slider, value, LV_ANIM_OFF);
    }
    if (previous_volume == (int)value) {
        return;
    }
    previous_volume = value;
    if (value) {
        unmuted_volume = value;
    }
    char caption[16];
    snprintf(caption, sizeof(caption), "%u%%", value);
    set_label_text(volume_label, caption);
    set_label_text(mute_label, value ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_MUTE);
    lv_obj_set_style_text_color(mute_label,
                                lv_color_hex(value ? COLOR_CYAN : COLOR_ORANGE), 0);
}

static void volume_changed(lv_event_t *event)
{
    (void)event;
    media_set_volume((unsigned)lv_slider_get_value(volume_slider));
    refresh_volume();
}

static void toggle_mute(lv_event_t *event)
{
    (void)event;
    unsigned value = media_get_volume();
    if (value) {
        unmuted_volume = value;
        media_set_volume(0);
    } else {
        media_set_volume(unmuted_volume);
    }
    refresh_volume();
}

static void draw_bulb(lv_event_t *event)
{
    lv_obj_t *object = lv_event_get_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(object, &bounds);
    int32_t x = (bounds.x1 + bounds.x2 + 1) / 2;
    int32_t y = (bounds.y1 + bounds.y2 + 1) / 2 + 1;
    lv_color_t color = lv_color_hex(bulb_color);

    /* A soft badge and small moving sparkles keep the mascot inside its bounds. */
    lv_draw_rect_dsc_t disc;
    lv_draw_rect_dsc_init(&disc);
    disc.radius = LV_RADIUS_CIRCLE;
    disc.bg_color = lv_color_hex(COLOR_PANEL);
    disc.border_color = lv_color_hex(COLOR_LINE);
    disc.border_width = 2;
    lv_area_t area = {x - 88, y - 89, x + 88, y + 87};
    lv_draw_rect(layer, &disc, &area);
    disc.bg_color = color;
    disc.bg_opa = LV_OPA_10;
    disc.border_width = 0;
    lv_draw_rect(layer, &disc, &area);

    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    bool animated = bulb_state == BULB_CONNECTING || bulb_state == BULB_THINKING ||
                    bulb_state == BULB_SPEAKING;
    unsigned phase = animated ? (lv_tick_get() / 160) % 12 : 0;
    int bob = phase <= 6 ? (int)phase : 12 - (int)phase;
    draw_sparkle(layer, x - 70, y - 65 + bob, 6, bulb_color);
    draw_sparkle(layer, x + 70, y + 55 - bob, 5, bulb_color);
    draw_disc(layer, x + 56, y - 76, 4, COLOR_PEACH);
    draw_disc(layer, x - 61, y + 70, 3, COLOR_PINK);

    arc.center = (lv_point_t){x, y};
    arc.radius = 30;
    arc.start_angle = 140;
    arc.end_angle = 400;
    arc.width = 5;
    arc.color = color;
    arc.opa = LV_OPA_COVER;
    arc.rounded = true;
    lv_draw_arc(layer, &arc);

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = 5;
    line.round_start = true;
    line.round_end = true;
    static const int8_t segments[][4] = {
        {-21, 18, -14, 30}, {21, 18, 14, 30},
        {-14, 30, 14, 30}, {-13, 40, 13, 40}, {-8, 49, 8, 49},
        {0, -43, 0, -51}, {-34, -32, -40, -38}, {34, -32, 40, -38},
        {-43, 0, -51, 0}, {43, 0, 51, 0},
    };
    for (size_t i = 0; i < sizeof(segments) / sizeof(segments[0]); ++i) {
        line.p1 = (lv_point_precise_t){x + segments[i][0], y + segments[i][1]};
        line.p2 = (lv_point_precise_t){x + segments[i][2], y + segments[i][3]};
        lv_draw_line(layer, &line);
    }

    draw_disc(layer, x - 18, y + 8, 4, COLOR_PINK);
    draw_disc(layer, x + 18, y + 8, 4, COLOR_PINK);
    draw_disc(layer, x - 9, y - 2, 2, bulb_color);
    draw_disc(layer, x + 9, y - 2, 2, bulb_color);
    arc.center = (lv_point_t){x, y + 4};
    arc.radius = 7;
    arc.width = 2;
    arc.start_angle = bulb_state == BULB_ERROR ? 200 : 20;
    arc.end_angle = bulb_state == BULB_ERROR ? 340 : 160;
    lv_draw_arc(layer, &arc);
}

static void update_conversation_indicator(const app_snapshot_t *state,
                                           bool connecting, bool stopping,
                                           bool running)
{
    const char *caption;
    const char *hint;
    bulb_state_t next;
    if (stopping) {
        caption = "Stopping...";
        hint = "";
        next = BULB_STOPPED;
    } else if (connecting) {
        caption = "Connecting...";
        hint = "";
        next = BULB_CONNECTING;
    } else if (running) {
        bool speaking = strcmp(state->agent_state, "speaking") == 0 ||
                        media_is_speaking();
        bool thinking = strcmp(state->agent_state, "thinking") == 0;
        caption = speaking ? "Speaking" : thinking ? "Thinking" : "Listening";
        hint = speaking ? "" : thinking ?
               "Your assistant is preparing a reply" : "Go ahead, I'm listening";
        next = speaking ? BULB_SPEAKING : thinking ? BULB_THINKING : BULB_LISTENING;
    } else {
        caption = state->phase == APP_ERROR ? "Connection failed" : "Ready to go";
        hint = state->phase == APP_ERROR ? state->message : "";
        next = state->phase == APP_ERROR ? BULB_ERROR : BULB_STOPPED;
    }
    static const unsigned colors[] = {
        [BULB_STOPPED] = COLOR_MUTED,
        [BULB_CONNECTING] = COLOR_CYAN,
        [BULB_LISTENING] = COLOR_GREEN,
        [BULB_THINKING] = COLOR_YELLOW,
        [BULB_SPEAKING] = COLOR_ORANGE,
        [BULB_ERROR] = COLOR_RED,
    };
    if (bulb_state != next) {
        bulb_state = next;
        bulb_color = colors[next];
        lv_obj_set_style_text_color(conversation_status_label, lv_color_hex(bulb_color), 0);
        lv_obj_invalidate(conversation_indicator);
    }
    set_label_text(hero_heading, "VOICE ASSISTANT");
    set_label_text(conversation_status_label, caption);
    set_label_text(conversation_hint, hint);
    set_visible(conversation_hint, hint[0] != '\0');
    lv_obj_remove_flag(conversation_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(conversation_status_label, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_scan(lv_event_t *event)
{
    (void)event;
    network_scan_start();
}

static void connect_selected(void)
{
    char password[65] = {0};
    if (selected_ap.secure) {
        const char *entered = lv_textarea_get_text(password_input);
        if (strlen(entered) < 8 || strlen(entered) > 63) {
            lv_label_set_text(password_status_label, "Password must be 8-63 characters");
            return;
        }
        strcpy(password, entered);
    }
    esp_err_t result = network_connect(selected_ap.ssid, password);
    memset(password, 0, sizeof(password));
    lv_textarea_set_text(password_input, "");
    if (result == ESP_OK) {
        wifi_connect_pending = true;
        show_wifi_page(false);
    } else {
        lv_label_set_text_fmt(password_status_label, "Connection could not start (%s)",
                              esp_err_to_name(result));
    }
}

static void connect_pressed(lv_event_t *event)
{
    (void)event;
    connect_selected();
}

static void password_ready(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_READY) {
        connect_selected();
    }
}

static void select_ap(lv_event_t *event)
{
    network_status_t network;
    network_get_status(&network);
    update_wifi_status_button(&network);
    if (network.scanning) {
        lv_label_set_text(wifi_status_label, "Wait for the scan to finish");
        return;
    }
    if (network.connection == NETWORK_CONNECTING) {
        lv_label_set_text(wifi_status_label, "Wait for the current connection attempt");
        return;
    }
    network_ap_t *ap = lv_event_get_user_data(event);
    selected_ap = *ap;
    if (!selected_ap.secure) {
        connect_selected();
        return;
    }
    lv_label_set_text_fmt(selected_label, "Password for %s", selected_ap.ssid);
    lv_label_set_text(password_status_label, "Enter the Wi-Fi password");
    lv_textarea_set_text(password_input, "");
    lv_obj_add_flag(wifi_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(password_page, LV_OBJ_FLAG_HIDDEN);
}

static void draw_wifi_row(lv_event_t *event)
{
    const network_ap_t *ap = lv_event_get_user_data(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target_obj(event), &bounds);
    lv_area_t text_area = {bounds.x1 + 12, bounds.y1 + 7, bounds.x2 - 12, bounds.y1 + 29};
    lv_draw_label_dsc_t text;
    lv_draw_label_dsc_init(&text);
    text.font = &lv_font_source_han_sans_sc_16_cjk;
    text.color = lv_color_hex(COLOR_TEXT);
    text.text = ap->ssid;
    lv_draw_label(layer, &text, &text_area);

    char detail[64];
    snprintf(detail, sizeof(detail), "%s  /  %d dBm",
             ap->secure ? "Password protected" : "Open network", ap->rssi);
    text.font = LV_FONT_DEFAULT;
    text.color = lv_color_hex(COLOR_MUTED);
    text.text = detail;
    text.text_local = true;
    text_area.y1 = bounds.y1 + 33;
    text_area.y2 = bounds.y2 - 4;
    lv_draw_label(layer, &text, &text_area);
}

static void show_scan_results(void)
{
    size_t count = network_get_scan_results(displayed_aps, NETWORK_MAX_APS);
    lv_obj_clean(ap_list);
    if (!count) {
        label(ap_list, "No networks found. Tap Scan again.", 12, 12,
              LV_FONT_DEFAULT, COLOR_MUTED);
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        /* One object per AP and shared styles keep all 20 APs within 64 KiB. */
        lv_obj_t *row = lv_obj_create(ap_list);
        lv_obj_remove_style_all(row);
        lv_obj_add_style(row, &wifi_row_style, 0);
        lv_obj_add_style(row, &wifi_row_pressed_style, LV_STATE_PRESSED);
        lv_obj_set_pos(row, 8, 8 + (int)i * 64);
        lv_obj_set_size(row, 408, 58);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, draw_wifi_row, LV_EVENT_DRAW_MAIN, &displayed_aps[i]);
        lv_obj_add_event_cb(row, select_ap, LV_EVENT_CLICKED, &displayed_aps[i]);
    }
}

static void refresh_wifi_status(void)
{
    network_status_t network;
    network_get_status(&network);
    char message[128];
    if (network.connection == NETWORK_CONNECTED) {
        snprintf(message, sizeof(message), "Connected to %s", network.ssid);
    } else if (network.connection == NETWORK_CONNECTING) {
        snprintf(message, sizeof(message), "Connecting to %s...", network.ssid);
    } else if (network.connection == NETWORK_FAILED) {
        if (network.disconnect_reason == WIFI_REASON_AUTH_FAIL ||
            network.disconnect_reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
            network.disconnect_reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) {
            strcpy(message, "Connection failed. Check the password and retry.");
        } else if (network.disconnect_reason == WIFI_REASON_NO_AP_FOUND) {
            strcpy(message, "Network not found. Scan and select it again.");
        } else {
            snprintf(message, sizeof(message), "Connection failed (reason %d). Select a network to retry.",
                     network.disconnect_reason);
        }
    } else {
        strcpy(message, "Select a Wi-Fi network to connect");
    }
    if (!network.scanning && network.scan_error != ESP_OK &&
        network.connection != NETWORK_CONNECTED) {
        lv_label_set_text_fmt(wifi_status_label, "Scan failed (%s). Tap Scan to retry.",
                              esp_err_to_name(network.scan_error));
    } else {
        lv_label_set_text(wifi_status_label, message);
    }
    set_label_text(scan_button_label, network.scanning ? "Scanning..." : LV_SYMBOL_REFRESH " Scan networks");
    if (network.scanning) {
        lv_obj_add_state(scan_button, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(scan_button, LV_STATE_DISABLED);
    }
    if (network.scan_generation != displayed_generation) {
        displayed_generation = network.scan_generation;
        show_scan_results();
    }
}

static bool conversation_busy(const app_snapshot_t *state)
{
    return state->phase == APP_STOPPING || state->phase == APP_STARTING ||
           state->phase == APP_CONNECTING ||
           (session_auto_start_pending() && state->phase != APP_ERROR) ||
           (session_requested() && state->phase != APP_RUNNING && state->phase != APP_ERROR);
}

static void start_stop(lv_event_t *event)
{
    (void)event;
    app_snapshot_t state;
    app_get_snapshot(&state);
    if (!network_is_connected()) {
        show_wifi_page(true);
        return;
    }
    if (state.phase == APP_STOPPING ||
        (state.pairing_ready && conversation_busy(&state))) {
        return;
    }
    if (!state.pairing_ready) {
        if (state.phase == APP_ERROR) {
            app_set_status(APP_REQUESTING_PAIR_CODE, "Retrying MyBot setup");
            esp_err_t result = bot_station_retry_pairing();
            if (result != ESP_OK) {
                app_set_status(APP_ERROR, "Could not retry MyBot setup");
            }
        }
        return;
    }
    if (session_requested()) {
        session_request(false);
    } else {
        session_request(true);
    }
}

static void ai_button_clicked(void *handle, void *user_data)
{
    (void)handle;
    (void)user_data;
    /* The button's timer task must never block on LVGL or an HTTP request. */
    atomic_store(&ai_button_pending, true);
}

static void init_ai_button(void)
{
    button_handle_t buttons[BSP_BUTTON_NUM] = {0};
    int count = 0;
    esp_err_t result = bsp_iot_button_create(buttons, &count, BSP_BUTTON_NUM);
    if (result == ESP_OK) {
        result = iot_button_register_cb(buttons[BSP_BUTTON_AI], BUTTON_SINGLE_CLICK,
                                        NULL, ai_button_clicked, NULL);
    }
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Orange AI button ready: short click toggles conversation (GPIO %d)",
                 BSP_BUTTON_AI_GPIO);
    } else {
        for (int i = 0; i < BSP_BUTTON_NUM; ++i) {
            if (buttons[i]) {
                iot_button_delete(buttons[i]);
            }
        }
        ESP_LOGW(TAG, "AI button unavailable: %s; use the screen control", esp_err_to_name(result));
    }
}

static void show_waiting_prompt(const char *text)
{
    if (pair_code_mode) {
        lv_obj_set_style_text_font(pair_code_label, &lv_font_montserrat_28, 0);
        pair_code_mode = false;
    }
    if (strcmp(lv_label_get_text(pair_code_label), text) != 0) {
        lv_label_set_text(pair_code_label, text);
        lv_obj_center(pair_code_label);
    }
    lv_obj_remove_flag(pair_code_label, LV_OBJ_FLAG_HIDDEN);
}

static void refresh(lv_timer_t *timer)
{
    (void)timer;
    if (atomic_exchange(&ai_button_pending, false)) {
        ESP_LOGI(TAG, "AI button: conversation toggle requested");
        start_stop(NULL);
    }
    refresh_clock();
    refresh_volume();
    if (brightness_pending && lv_tick_elaps(brightness_applied_ms) >= 100) {
        apply_brightness();
    }
    refresh_power();
    app_snapshot_t state;
    app_get_snapshot(&state);
    refresh_session_info(&state);
    network_status_t network;
    network_get_status(&network);
    update_wifi_status_button(&network);
    set_visible(duration_label, state.pairing_ready);
    set_visible(duration_heading, state.pairing_ready);
    set_visible(link_label, state.pairing_ready);
    set_visible(link_heading, state.pairing_ready);
    set_visible(details_button, state.pairing_ready);
    set_visible(conversation_status_label, true);
    set_visible(conversation_hint, true);
    if (network.connection != NETWORK_CONNECTED) {
        lv_obj_add_flag(conversation_indicator, LV_OBJ_FLAG_HIDDEN);
        set_label_text(hero_heading, "NETWORK");
        set_label_text(conversation_status_label, "Let's get connected");
        set_label_text(conversation_hint, "Tap Wi-Fi above to choose a network");
        /* Provisioning remains accessible when saved credentials stop working. */
        if (network.connection == NETWORK_FAILED) {
            if (lv_obj_has_flag(wifi_page, LV_OBJ_FLAG_HIDDEN) &&
                lv_obj_has_flag(password_page, LV_OBJ_FLAG_HIDDEN)) {
                show_wifi_page(true);
            }
        }
        refresh_wifi_status();
        lv_obj_add_flag(action, LV_OBJ_FLAG_HIDDEN);
        bool provisioning =
            !lv_obj_has_flag(wifi_page, LV_OBJ_FLAG_HIDDEN) ||
            !lv_obj_has_flag(password_page, LV_OBJ_FLAG_HIDDEN);
        if (provisioning) {
            lv_obj_add_flag(pair_code_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            show_waiting_prompt("Connecting Wi-Fi");
        }
        return;
    }

    if (!lv_obj_has_flag(wifi_page, LV_OBJ_FLAG_HIDDEN)) {
        refresh_wifi_status();
    }

    /* Keep the Wi-Fi status page open unless it was waiting for a new IP. */
    if (wifi_connect_pending) {
        wifi_connect_pending = false;
        if (!lv_obj_has_flag(password_page, LV_OBJ_FLAG_HIDDEN)) {
            lv_textarea_set_text(password_input, "");
        }
        close_wifi(NULL);
    }

    if (state.pair_code[0]) {
        lv_obj_add_flag(conversation_indicator, LV_OBJ_FLAG_HIDDEN);
        set_label_text(hero_heading, "PAIR YOUR DEVICE");
        set_label_text(conversation_status_label, "Your pairing code");
        set_label_text(conversation_hint, "Enter this code in Bot Station to get started");
        lv_obj_add_flag(action, LV_OBJ_FLAG_HIDDEN);
        if (!pair_code_mode) {
            lv_obj_set_style_text_font(pair_code_label, &pair_code_font, 0);
            pair_code_mode = true;
        }
        if (strcmp(lv_label_get_text(pair_code_label), state.pair_code) != 0) {
            lv_label_set_text(pair_code_label, state.pair_code);
            lv_obj_center(pair_code_label);
            ESP_LOGI(TAG, "Pair code label updated");
        }
        lv_obj_remove_flag(pair_code_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (!state.pairing_ready) {
        lv_obj_add_flag(conversation_indicator, LV_OBJ_FLAG_HIDDEN);
        set_label_text(hero_heading, "DEVICE SETUP");
        set_label_text(conversation_status_label, state.phase == APP_ERROR ?
                       "Setup needs attention" : "Getting things ready");
        set_label_text(conversation_hint, state.phase == APP_ERROR ? state.message :
                       "Your conversation will start automatically");
        if (state.phase == APP_ERROR) {
            lv_obj_remove_flag(action, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_state(action, LV_STATE_DISABLED);
            set_label_text(action_label, "Retry");
            lv_obj_set_style_text_font(action_label, &lv_font_montserrat_20, 0);
            lv_obj_set_style_bg_color(action, lv_color_hex(COLOR_RAISED), 0);
        } else {
            lv_obj_add_flag(action, LV_OBJ_FLAG_HIDDEN);
        }
        const char *waiting = state.phase == APP_WAITING_FOR_WIFI ?
                              "Waiting for Wi-Fi" :
                              state.phase == APP_SYNCING_TIME ?
                              "Connecting..." :
                              state.phase == APP_VERIFYING_BINDING ?
                              "Checking device" :
                              state.phase == APP_REQUESTING_PAIR_CODE ?
                              "Requesting code" :
                              state.phase == APP_ERROR ?
                              "Connection failed" : "Connecting";
        show_waiting_prompt(waiting);
        return;
    }

    lv_obj_add_flag(pair_code_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(action, LV_OBJ_FLAG_HIDDEN);
    bool stopping = state.phase == APP_STOPPING;
    /* Binding is ready before the session task consumes its automatic start. */
    bool connecting = !stopping && conversation_busy(&state);
    bool running = state.phase == APP_RUNNING;
    bool busy = stopping || connecting;
    update_conversation_indicator(&state, connecting, stopping, running);
    if ((bulb_state == BULB_CONNECTING || bulb_state == BULB_THINKING ||
         bulb_state == BULB_SPEAKING) &&
        lv_obj_has_flag(wifi_page, LV_OBJ_FLAG_HIDDEN) &&
        lv_obj_has_flag(password_page, LV_OBJ_FLAG_HIDDEN) &&
        lv_obj_has_flag(details_page, LV_OBJ_FLAG_HIDDEN) &&
        lv_obj_has_flag(brightness_sheet, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_invalidate(conversation_indicator);
    }
    if (busy) {
        lv_obj_add_state(action, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(action, LV_STATE_DISABLED);
    }
    const char *caption = stopping ? "Stopping..." :
                          connecting ? "Connecting..." :
                          running ? LV_SYMBOL_STOP " Stop" :
                          state.phase == APP_ERROR ? LV_SYMBOL_REFRESH " Retry" : LV_SYMBOL_PLAY " Start";
    if (strcmp(lv_label_get_text(action_label), caption) != 0) {
        lv_label_set_text(action_label, caption);
        lv_obj_set_style_text_font(action_label, busy ? LV_FONT_DEFAULT : &lv_font_montserrat_20, 0);
        lv_obj_set_style_bg_color(action, lv_color_hex(running ? COLOR_PINK : COLOR_MINT), 0);
        lv_obj_set_style_text_color(action_label,
                                    lv_color_hex(running ? COLOR_RED : COLOR_CYAN), 0);
    }
}

esp_err_t ui_init(void)
{
    if (!bsp_display_start() || !bsp_display_lock(-1)) {
        return ESP_FAIL;
    }
    lv_style_init(&wifi_row_style);
    lv_style_set_bg_color(&wifi_row_style, lv_color_hex(COLOR_BG));
    lv_style_set_bg_opa(&wifi_row_style, LV_OPA_COVER);
    lv_style_set_radius(&wifi_row_style, 18);
    lv_style_set_text_color(&wifi_row_style, lv_color_hex(COLOR_TEXT));
    lv_style_init(&wifi_row_pressed_style);
    lv_style_set_bg_color(&wifi_row_pressed_style, lv_color_hex(COLOR_PEACH));
    lv_obj_t *screen = lv_screen_active();
    style_page(screen);

    battery_label = label(screen, LV_SYMBOL_BATTERY_EMPTY "  --", 24, 13,
                           &lv_font_montserrat_28, COLOR_MUTED);
    battery_status_label = label(screen, "", 25, 47, LV_FONT_DEFAULT, COLOR_MUTED);
    clock_label = label(screen, "--:--", 24, 67, &lv_font_montserrat_40, COLOR_TEXT);
    seconds_label = label(screen, "--", 164, 82, &lv_font_montserrat_20, COLOR_CYAN);
    date_label = label(screen, "Date pending", 250, 74, LV_FONT_DEFAULT, COLOR_TEXT);
    clock_hint = label(screen, "BEIJING / UTC+8", 250, 96, LV_FONT_DEFAULT, COLOR_MUTED);
    hero_heading = centered_label(screen, "VOICE ASSISTANT", 122, LV_FONT_DEFAULT, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(hero_heading, 1, 0);

    /* A native digit font avoids a transform layer larger than LVGL's free pool. */
    pair_code_label = label(screen, "", 0, 0, &pair_code_font, COLOR_CYAN);
    lv_obj_set_width(pair_code_label, 432);
    lv_obj_set_style_text_align(pair_code_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(pair_code_label);
    lv_obj_add_flag(pair_code_label, LV_OBJ_FLAG_HIDDEN);
    action = button(screen, "Connecting...", 170, 380, 140, 44, COLOR_MINT);
    lv_obj_add_flag(action, LV_OBJ_FLAG_HIDDEN);
    action_label = lv_obj_get_child(action, 0);
    lv_obj_set_style_text_font(action_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(action_label, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_add_event_cb(action, start_stop, LV_EVENT_CLICKED, NULL);

    conversation_indicator = panel(screen, 0, 0, 204, 204, COLOR_BG, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(conversation_indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(conversation_indicator, 0, 0);
    lv_obj_center(conversation_indicator);
    lv_obj_add_event_cb(conversation_indicator, draw_bulb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(conversation_indicator, open_details, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(conversation_indicator, LV_OBJ_FLAG_HIDDEN);
    conversation_status_label = centered_label(screen, "Getting things ready", 342,
                                                &lv_font_montserrat_20, COLOR_TEXT);
    conversation_hint = centered_label(screen, "", 366, LV_FONT_DEFAULT, COLOR_MUTED);
    duration_heading = label(screen, "SESSION", 24, 210, LV_FONT_DEFAULT, COLOR_MUTED);
    duration_label = label(screen, "00:00", 24, 233, &lv_font_montserrat_20, COLOR_TEXT);
    link_heading = label(screen, "VOICE", 378, 210, LV_FONT_DEFAULT, COLOR_MUTED);
    link_label = label(screen, "IDLE", 378, 233, &lv_font_montserrat_20, COLOR_CYAN);
    brightness_button = button(screen, "--", 24, 380, 114, 44, COLOR_BUTTER);
    brightness_button_label = lv_obj_get_child(brightness_button, 0);
    lv_obj_set_style_text_color(brightness_button_label, lv_color_hex(COLOR_ORANGE), 0);
    lv_obj_align(brightness_button_label, LV_ALIGN_RIGHT_MID, -15, 0);
    lv_obj_add_event_cb(brightness_button, draw_sun, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(brightness_button, open_brightness, LV_EVENT_CLICKED, NULL);
    details_button = button(screen, LV_SYMBOL_LIST " Chat", 342, 380, 114, 44, COLOR_SKY);
    lv_obj_set_style_text_color(details_button, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_add_event_cb(details_button, open_details, LV_EVENT_CLICKED, NULL);

    lv_obj_t *volume_panel = panel(screen, 12, 430, 456, 50, COLOR_PANEL, 24);
    label(volume_panel, "SPEAKER", 80, 2, LV_FONT_DEFAULT, COLOR_MUTED);
    volume_label = label(volume_panel, "", 350, 2, LV_FONT_DEFAULT, COLOR_TEXT);
    lv_obj_set_width(volume_label, 50);
    lv_obj_set_style_text_align(volume_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *mute_button = button(volume_panel, LV_SYMBOL_VOLUME_MID, 20, 3, 48, 44, COLOR_PANEL);
    mute_label = lv_obj_get_child(mute_button, 0);
    lv_obj_set_style_text_font(mute_label, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(mute_button, toggle_mute, LV_EVENT_CLICKED, NULL);
    label(volume_panel, LV_SYMBOL_VOLUME_MAX, 426, 22, &lv_font_montserrat_20, COLOR_MUTED);
    volume_slider = lv_slider_create(volume_panel);
    lv_obj_set_pos(volume_slider, 82, 31);
    lv_obj_set_size(volume_slider, 316, 7);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(COLOR_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(COLOR_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(COLOR_CYAN), LV_PART_KNOB);
    lv_obj_set_style_pad_all(volume_slider, 7, LV_PART_KNOB);
    lv_obj_set_ext_click_area(volume_slider, 14);
    lv_obj_add_event_cb(volume_slider, volume_changed, LV_EVENT_VALUE_CHANGED, NULL);
    refresh_volume();

    wifi_status_button = button(screen, "", 240, 14, 216, 46, COLOR_PANEL);
    wifi_connection_label = label(wifi_status_button, "SET UP WI-FI", 38, 4,
                                  LV_FONT_DEFAULT, COLOR_MUTED);
    wifi_ssid_label = label(wifi_status_button, "Tap to connect", 38, 24,
                            &lv_font_source_han_sans_sc_16_cjk, COLOR_TEXT);
    lv_obj_set_width(wifi_ssid_label, 168);
    lv_label_set_long_mode(wifi_ssid_label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *wifi_icon = lv_label_create(wifi_status_button);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(wifi_icon, 10, 15);
    lv_obj_add_event_cb(wifi_status_button, open_wifi, LV_EVENT_CLICKED, NULL);
    wifi_page = panel(screen, 0, 0, 480, 480, COLOR_BG, 0);
    style_page(wifi_page);
    label(wifi_page, "Wi-Fi setup", 24, 20, &lv_font_montserrat_28, COLOR_TEXT);
    lv_obj_t *wifi_close = button(wifi_page, "Close", 364, 18, 92, 44, COLOR_RAISED);
    lv_obj_add_event_cb(wifi_close, close_wifi, LV_EVENT_CLICKED, NULL);
    wifi_status_label = label(wifi_page, "Select a Wi-Fi network to connect", 24, 69,
                              &lv_font_source_han_sans_sc_16_cjk, COLOR_MUTED);
    lv_obj_set_width(wifi_status_label, 432);
    lv_label_set_long_mode(wifi_status_label, LV_LABEL_LONG_MODE_WRAP);
    scan_button = button(wifi_page, LV_SYMBOL_REFRESH " Scan networks", 24, 112, 432, 46, COLOR_MINT);
    lv_obj_set_style_text_color(scan_button, lv_color_hex(COLOR_CYAN), 0);
    scan_button_label = lv_obj_get_child(scan_button, 0);
    lv_obj_add_event_cb(scan_button, refresh_scan, LV_EVENT_CLICKED, NULL);
    ap_list = panel(wifi_page, 24, 169, 432, 284, COLOR_PANEL, 24);
    lv_obj_add_flag(ap_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ap_list, LV_DIR_VER);
    label(ap_list, "Scanning for networks...", 12, 12, LV_FONT_DEFAULT, COLOR_MUTED);
    lv_obj_add_flag(wifi_page, LV_OBJ_FLAG_HIDDEN);

    password_page = panel(screen, 0, 0, 480, 480, COLOR_BG, 0);
    style_page(password_page);
    label(password_page, "Join Wi-Fi", 24, 20, &lv_font_montserrat_28, COLOR_TEXT);
    lv_obj_t *back = button(password_page, "Back", 374, 18, 82, 44, COLOR_RAISED);
    lv_obj_add_event_cb(back, open_wifi, LV_EVENT_CLICKED, NULL);
    selected_label = label(password_page, "Password", 24, 70,
                           &lv_font_source_han_sans_sc_16_cjk, COLOR_MUTED);
    lv_obj_set_width(selected_label, 432);
    lv_label_set_long_mode(selected_label, LV_LABEL_LONG_MODE_DOTS);
    password_input = lv_textarea_create(password_page);
    lv_obj_set_pos(password_input, 24, 99);
    lv_obj_set_size(password_input, 432, 50);
    lv_textarea_set_one_line(password_input, true);
    lv_textarea_set_password_mode(password_input, true);
    lv_textarea_set_max_length(password_input, 63);
    lv_textarea_set_placeholder_text(password_input, "Wi-Fi password");
    lv_obj_set_style_bg_color(password_input, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_text_color(password_input, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_border_color(password_input, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_set_style_radius(password_input, 20, 0);
    lv_obj_set_style_border_width(password_input, 1, 0);
    lv_obj_set_style_text_color(password_input, lv_color_hex(COLOR_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_event_cb(password_input, password_ready, LV_EVENT_READY, NULL);
    password_status_label = label(password_page, "Enter the Wi-Fi password", 24, 157,
                                  LV_FONT_DEFAULT, COLOR_MUTED);
    lv_obj_set_width(password_status_label, 286);
    lv_label_set_long_mode(password_status_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *join = button(password_page, "Connect", 322, 153, 134, 44, COLOR_MINT);
    lv_obj_set_style_text_color(join, lv_color_hex(COLOR_CYAN), 0);
    lv_obj_add_event_cb(join, connect_pressed, LV_EVENT_CLICKED, NULL);
    lv_obj_t *keyboard = lv_keyboard_create(password_page);
    /* lv_keyboard defaults to bottom alignment; reset it before using x/y coordinates. */
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 12, 204);
    lv_obj_set_size(keyboard, 456, 264);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_radius(keyboard, 24, 0);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_RAISED), LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, 12, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_MINT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(COLOR_CYAN), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_CYAN), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(COLOR_BG), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_keyboard_set_textarea(keyboard, password_input);
    lv_obj_add_flag(password_page, LV_OBJ_FLAG_HIDDEN);

    details_page = panel(screen, 0, 0, 480, 480, COLOR_BG, 0);
    style_page(details_page);
    label(details_page, "Conversation", 24, 20, &lv_font_montserrat_28, COLOR_TEXT);
    lv_obj_t *details_close = button(details_page, "Back", 374, 18, 82, 44, COLOR_RAISED);
    lv_obj_add_event_cb(details_close, close_details, LV_EVENT_CLICKED, NULL);
    details_status = label(details_page, "", 24, 76,
                            &lv_font_source_han_sans_sc_16_cjk, COLOR_CYAN);
    lv_obj_set_width(details_status, 432);
    lv_obj_set_height(details_status, 42);
    lv_obj_t *user_card = panel(details_page, 24, 128, 432, 120, COLOR_MINT, 24);
    label(user_card, LV_SYMBOL_AUDIO " YOU", 16, 14, LV_FONT_DEFAULT, COLOR_CYAN);
    user_message = label(user_card, "", 16, 44, &lv_font_source_han_sans_sc_16_cjk, COLOR_TEXT);
    lv_obj_set_size(user_message, 400, 68);
    lv_obj_t *assistant_card = panel(details_page, 24, 264, 432, 154, COLOR_PEACH, 24);
    label(assistant_card, LV_SYMBOL_VOLUME_MID " ASSISTANT", 16, 14, LV_FONT_DEFAULT, COLOR_ORANGE);
    assistant_message = label(assistant_card, "", 16, 44,
                               &lv_font_source_han_sans_sc_16_cjk, COLOR_TEXT);
    lv_obj_set_size(assistant_message, 400, 102);
    centered_label(details_page, "Latest messages from your assistant", 442, LV_FONT_DEFAULT, COLOR_MUTED);
    lv_obj_add_flag(details_page, LV_OBJ_FLAG_HIDDEN);

    brightness_sheet = panel(screen, 0, 0, 480, 480, COLOR_SCRIM, 0);
    lv_obj_set_style_bg_opa(brightness_sheet, LV_OPA_70, 0);
    lv_obj_add_event_cb(brightness_sheet, brightness_scrim_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *brightness_card = panel(brightness_sheet, 24, 260, 432, 196, COLOR_PANEL, 28);
    label(brightness_card, "Brightness", 20, 16, &lv_font_montserrat_28, COLOR_TEXT);
    lv_obj_t *brightness_done = button(brightness_card, "Done", 322, 12, 90, 44, COLOR_RAISED);
    lv_obj_add_event_cb(brightness_done, close_brightness, LV_EVENT_CLICKED, NULL);
    brightness_status = label(brightness_card, "Find your comfortable light", 24, 62,
                               LV_FONT_DEFAULT, COLOR_MUTED);
    lv_obj_set_width(brightness_status, 300);
    lv_label_set_long_mode(brightness_status, LV_LABEL_LONG_MODE_DOTS);
    brightness_value = label(brightness_card, "70%", 344, 62, LV_FONT_DEFAULT, COLOR_ORANGE);
    lv_obj_set_width(brightness_value, 64);
    lv_obj_set_style_text_align(brightness_value, LV_TEXT_ALIGN_RIGHT, 0);
    brightness_slider = lv_slider_create(brightness_card);
    lv_obj_set_pos(brightness_slider, 28, 99);
    lv_obj_set_size(brightness_slider, 376, 8);
    lv_slider_set_range(brightness_slider, DEVICE_BRIGHTNESS_MIN, 100);
    lv_slider_set_value(brightness_slider, DEVICE_BRIGHTNESS_DEFAULT, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(COLOR_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(COLOR_ORANGE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(COLOR_ORANGE), LV_PART_KNOB);
    lv_obj_set_style_pad_all(brightness_slider, 7, LV_PART_KNOB);
    lv_obj_set_ext_click_area(brightness_slider, 16);
    lv_obj_add_event_cb(brightness_slider, brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(brightness_slider, brightness_changed, LV_EVENT_RELEASED, NULL);
    static const unsigned presets[] = {20, 70, 100};
    static const char *preset_names[] = {"Dim", "Balanced", "Bright"};
    static const unsigned preset_colors[] = {COLOR_SKY, COLOR_MINT, COLOR_BUTTER};
    for (size_t i = 0; i < 3; ++i) {
        lv_obj_t *preset = button(brightness_card, preset_names[i], 24 + (int)i * 136,
                                  132, 112, 44, preset_colors[i]);
        lv_obj_add_event_cb(preset, brightness_preset, LV_EVENT_CLICKED, (void *)(uintptr_t)presets[i]);
    }
    lv_obj_add_flag(brightness_sheet, LV_OBJ_FLAG_HIDDEN);

    init_ai_button();
    refresh(NULL);
    lv_timer_create(refresh, 100, NULL);
    bsp_display_unlock();
    return ESP_OK;
}

void ui_show_wifi_settings(void)
{
    if (bsp_display_lock(-1)) {
        show_wifi_page(true);
        bsp_display_unlock();
    }
}

