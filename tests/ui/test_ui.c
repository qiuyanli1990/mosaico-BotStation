#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static time_t test_time;
static time_t host_time(time_t *destination)
{
    if (destination) {
        *destination = test_time;
    }
    return test_time;
}

/* Exercise production callbacks and widgets using the real LVGL renderer. */
#define time host_time
#include "../../main/ui.c"
#undef time

static app_snapshot_t test_state;
static network_status_t test_network;
static bool test_time_ready;
static bool test_requested;
static bool test_auto_start = true;
static bool test_speaking;
static unsigned test_volume = 70;
static unsigned request_calls;
static unsigned scan_calls;
static unsigned connect_calls;
static unsigned retry_calls;
static unsigned test_ap_count = 3;
static button_cb_t hardware_callback;
static void *hardware_user_data;
static bool test_button_unavailable;
static device_power_status_t test_power = {
    .battery = DEVICE_BATTERY_AVAILABLE, .battery_percent = 78,
    .brightness = 70, .brightness_available = true,
};
static unsigned brightness_calls;
static bool brightness_failed;
static uint16_t framebuffer[480 * 480];
static uint16_t render_buffer[480 * 32];
static const char *preview_directory;

void host_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}

const char *esp_err_to_name(esp_err_t error)
{
    return error == ESP_OK ? "ESP_OK" : "ESP_FAIL";
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    const uint16_t *source = (const uint16_t *)pixels;
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            assert(x >= 0 && x < 480 && y >= 0 && y < 480);
            framebuffer[y * 480 + x] = *source++;
        }
    }
    lv_display_flush_ready(display);
}

lv_display_t *bsp_display_start(void)
{
    lv_display_t *display = lv_display_create(480, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, render_buffer, NULL, sizeof(render_buffer),
                            LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    return display;
}

bool bsp_display_lock(int timeout_ms) { (void)timeout_ms; return true; }
void bsp_display_unlock(void) {}

esp_err_t bsp_iot_button_create(button_handle_t buttons[], int *count, int size)
{
    assert(size == BSP_BUTTON_NUM);
    if (test_button_unavailable) {
        return ESP_FAIL;
    }
    buttons[BSP_BUTTON_AI] = (void *)1;
    *count = BSP_BUTTON_NUM;
    return ESP_OK;
}

esp_err_t iot_button_register_cb(button_handle_t handle, button_event_t event,
                                void *args, button_cb_t callback, void *user_data)
{
    (void)handle;
    (void)args;
    assert(event == BUTTON_SINGLE_CLICK);
    hardware_callback = callback;
    hardware_user_data = user_data;
    return ESP_OK;
}

esp_err_t iot_button_delete(button_handle_t button) { (void)button; return ESP_OK; }
void network_get_status(network_status_t *status) { *status = test_network; }
bool network_is_connected(void) { return test_network.connection == NETWORK_CONNECTED; }
bool network_time_ready(void) { return test_time_ready; }

esp_err_t network_scan_start(void)
{
    scan_calls++;
    return ESP_OK;
}

size_t network_get_scan_results(network_ap_t *results, size_t capacity)
{
    const network_ap_t samples[] = {
        {.ssid = "Mosaico Studio", .rssi = -42, .secure = true},
        {.ssid = "Workshop Guest", .rssi = -58, .secure = false},
        {.ssid = "Home Network", .rssi = -72, .secure = true},
    };
    assert(capacity >= test_ap_count);
    for (unsigned i = 0; i < test_ap_count; ++i) {
        results[i] = samples[i % 3];
    }
    return test_ap_count;
}

esp_err_t network_connect(const char *ssid, const char *password)
{
    assert(ssid[0]);
    assert(!password[0] || strlen(password) >= 8);
    connect_calls++;
    test_network.connection = NETWORK_CONNECTING;
    return ESP_OK;
}

void app_get_snapshot(app_snapshot_t *state) { *state = test_state; }
void app_set_status(app_phase_t phase, const char *message)
{
    test_state.phase = phase;
    snprintf(test_state.message, sizeof(test_state.message), "%s", message);
}

bool session_requested(void) { return test_requested; }
bool session_auto_start_pending(void) { return test_auto_start; }
void session_request(bool start)
{
    request_calls++;
    test_auto_start = false;
    test_requested = start;
    test_state.phase = start ? APP_STARTING : APP_STOPPING;
}

esp_err_t bot_station_retry_pairing(void) { retry_calls++; return ESP_OK; }
unsigned media_get_volume(void) { return test_volume; }
void media_set_volume(unsigned volume) { assert(volume <= 100); test_volume = volume; }
bool media_is_speaking(void) { return test_speaking; }
void device_power_get_status(device_power_status_t *status) { *status = test_power; }
esp_err_t device_power_set_brightness(unsigned percent)
{
    assert(percent >= DEVICE_BRIGHTNESS_MIN && percent <= 100);
    brightness_calls++;
    if (brightness_failed) {
        return ESP_FAIL;
    }
    test_power.brightness = percent;
    test_power.brightness_available = true;
    return ESP_OK;
}

static bool visible(lv_obj_t *object)
{
    return !lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *find_button(lv_obj_t *parent, const char *caption)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        lv_obj_t *child = lv_obj_get_child(parent, (int)i);
        if (lv_obj_check_type(child, &lv_button_class)) {
            lv_obj_t *text = lv_obj_get_child(child, 0);
            if (text && lv_obj_check_type(text, &lv_label_class) &&
                !strcmp(lv_label_get_text(text), caption)) {
                return child;
            }
        }
        lv_obj_t *found = find_button(child, caption);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static void step(unsigned milliseconds)
{
    lv_tick_inc(milliseconds);
    refresh(NULL);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(NULL);
}

static void snapshot(const char *name)
{
    step(100);
    step(300);
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    printf("%s: LVGL free=%zu largest=%zu\n", name, memory.free_size, memory.free_biggest_size);
    fflush(stdout);
    if (!preview_directory) {
        return;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", preview_directory, name);
    FILE *output = fopen(path, "wb");
    assert(output);
    fprintf(output, "P6\n480 480\n255\n");
    for (unsigned i = 0; i < 480 * 480; ++i) {
        const uint16_t pixel = framebuffer[i];
        const unsigned char rgb[] = {
            (unsigned char)(((pixel >> 11) & 31) * 255 / 31),
            (unsigned char)(((pixel >> 5) & 63) * 255 / 63),
            (unsigned char)((pixel & 31) * 255 / 31),
        };
        fwrite(rgb, 1, 3, output);
    }
    assert(!fclose(output));
}

static void press_hardware(void)
{
    hardware_callback(NULL, hardware_user_data);
    step(100);
}

int main(int argc, char **argv)
{
    test_button_unavailable = argc > 1 && !strcmp(argv[1], "--button-unavailable");
    if (test_button_unavailable) {
        lv_init();
        assert(ui_init() == ESP_OK);
        assert(!hardware_callback);
        assert(strstr(lv_label_get_text(battery_label), "78%"));
        assert(visible(brightness_button));
        lv_deinit();
        puts("PASS: button initialization failure preserves the screen UI.");
        return 0;
    }
    preview_directory = argc > 1 ? argv[1] : NULL;
    lv_init();
    test_state.phase = APP_WAITING_FOR_WIFI;
    assert(ui_init() == ESP_OK);
    assert(strstr(lv_label_get_text(battery_label), "78%"));
    assert(lv_obj_get_style_text_font(battery_label, 0) == &lv_font_montserrat_28);
    step(100);
    assert(!visible(action));
    assert(!strcmp(lv_label_get_text(clock_label), "--:--"));
    press_hardware();
    assert(visible(wifi_page) && scan_calls == 1);
    assert(!request_calls);
    close_wifi(NULL);

    test_network.connection = NETWORK_CONNECTED;
    strcpy(test_network.ssid, "Mosaico Studio");
    test_time_ready = true;
    /* 2026-10-29 06:08:32 UTC -> 14:08:32 Beijing. */
    test_time = 1793254112;
    test_state.phase = APP_WAITING_FOR_BINDING;
    strcpy(test_state.pair_code, "682419");
    step(100);
    assert(!strcmp(lv_label_get_text(clock_label), "14:08"));
    assert(!strcmp(lv_label_get_text(seconds_label), "32"));
    assert(!strcmp(lv_label_get_text(date_label), "Thu, 29 Oct 2026"));
    assert(visible(pair_code_label) && !visible(action));
    assert(!strcmp(lv_label_get_text(pair_code_label), "682419"));
    snapshot("pairing");
    press_hardware();
    assert(!request_calls);

    test_state.pair_code[0] = 0;
    test_state.pairing_ready = true;
    test_state.phase = APP_PAIRED;
    step(100);
    assert(lv_obj_has_state(action, LV_STATE_DISABLED));
    assert(!strcmp(lv_label_get_text(action_label), "Connecting..."));
    press_hardware();
    assert(!request_calls);
    snapshot("connecting");

    test_auto_start = false;
    test_requested = true;
    test_state.phase = APP_RUNNING;
    strcpy(test_state.agent_state, "listening");
    strcpy(test_state.message, "Conversation connected");
    step(100);
    assert(bulb_state == BULB_LISTENING && bulb_color == COLOR_GREEN);
    assert(!lv_obj_has_state(action, LV_STATE_DISABLED));
    lv_area_t bounds;
    lv_obj_get_coords(conversation_indicator, &bounds);
    assert(bounds.x1 + bounds.x2 + 1 == 480);
    assert(bounds.y1 + bounds.y2 + 1 == 480);
    assert(lv_obj_get_y(action) > bounds.y2);
    step(125000);
    assert(!strcmp(lv_label_get_text(duration_label), "02:05"));
    snapshot("listening");

    lv_obj_send_event(brightness_button, LV_EVENT_CLICKED, NULL);
    assert(visible(brightness_sheet));
    assert(lv_slider_get_value(brightness_slider) == 70);
    for (int percent = 47; percent <= 49; ++percent) {
        lv_slider_set_value(brightness_slider, percent, LV_ANIM_OFF);
        lv_obj_send_event(brightness_slider, LV_EVENT_VALUE_CHANGED, NULL);
    }
    assert(!brightness_calls);
    step(100);
    assert(brightness_calls == 1 && test_power.brightness == 49);
    lv_slider_set_value(brightness_slider, 10, LV_ANIM_OFF);
    lv_obj_send_event(brightness_slider, LV_EVENT_RELEASED, NULL);
    assert(test_power.brightness == 10);
    lv_obj_send_event(find_button(brightness_sheet, "Bright"), LV_EVENT_CLICKED, NULL);
    assert(test_power.brightness == 100);
    brightness_failed = true;
    lv_obj_send_event(find_button(brightness_sheet, "Dim"), LV_EVENT_CLICKED, NULL);
    assert(test_power.brightness == 100 && lv_slider_get_value(brightness_slider) == 100);
    assert(strstr(lv_label_get_text(brightness_status), "Could not adjust"));
    brightness_failed = false;
    lv_obj_send_event(find_button(brightness_sheet, "Balanced"), LV_EVENT_CLICKED, NULL);
    assert(test_power.brightness == 70);
    assert(test_volume == 70 && request_calls == 0);
    snapshot("brightness");
    lv_obj_send_event(find_button(brightness_sheet, "Done"), LV_EVENT_CLICKED, NULL);
    assert(!visible(brightness_sheet));
    lv_obj_send_event(brightness_button, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(brightness_sheet, LV_EVENT_CLICKED, NULL);
    assert(!visible(brightness_sheet));

    test_power.battery_percent = 0;
    step(100);
    assert(strstr(lv_label_get_text(battery_label), "0%"));
    test_power.battery_percent = 18;
    snapshot("battery-low");
    assert(lv_color_eq(lv_obj_get_style_text_color(battery_label, 0), lv_color_hex(COLOR_RED)));
    test_power.battery_percent = 64;
    test_power.charging = true;
    snapshot("battery-charging");
    assert(strstr(lv_label_get_text(battery_status_label), "Charging"));
    assert(visible(battery_status_label));
    test_power.charging = false;
    test_power.battery_percent = 100;
    test_power.full = true;
    step(100);
    assert(strstr(lv_label_get_text(battery_status_label), "Full"));
    test_power.full = false;
    test_power.battery = DEVICE_BATTERY_UNAVAILABLE;
    snapshot("battery-unavailable");
    assert(strstr(lv_label_get_text(battery_label), "--"));
    assert(!strchr(lv_label_get_text(battery_label), '%'));
    assert(!strcmp(lv_label_get_text(battery_status_label), "Battery unavailable"));
    test_power.battery = DEVICE_BATTERY_ABSENT;
    step(100);
    assert(!strcmp(lv_label_get_text(battery_status_label), "No battery"));
    test_power.battery = DEVICE_BATTERY_AVAILABLE;
    test_power.battery_percent = 78;

    strcpy(test_state.agent_state, "thinking");
    step(100);
    assert(bulb_color == COLOR_YELLOW);
    assert(visible(conversation_hint));
    snapshot("thinking");
    strcpy(test_state.agent_state, "speaking");
    step(100);
    assert(bulb_color == COLOR_ORANGE);
    assert(!strcmp(lv_label_get_text(conversation_status_label), "Speaking"));
    assert(!visible(conversation_hint));
    snapshot("speaking");
    strcpy(test_state.agent_state, "listening");
    test_speaking = true;
    step(100);
    assert(bulb_state == BULB_SPEAKING);
    assert(!visible(conversation_hint));
    test_speaking = false;

    lv_obj_send_event(conversation_indicator, LV_EVENT_CLICKED, NULL);
    assert(visible(details_page));
    strcpy(test_state.user_transcript, "What can we build together today?");
    strcpy(test_state.assistant_transcript, "Let's turn your next idea into something real.");
    snapshot("conversation");
    assert(!strcmp(lv_label_get_text(user_message), test_state.user_transcript));
    close_details(NULL);

    toggle_mute(NULL);
    assert(test_volume == 0 && lv_slider_get_value(volume_slider) == 0);
    toggle_mute(NULL);
    assert(test_volume == 70);
    lv_slider_set_value(volume_slider, 37, LV_ANIM_OFF);
    lv_obj_send_event(volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(test_volume == 37);
    toggle_mute(NULL);
    toggle_mute(NULL);
    assert(test_volume == 37);
    lv_slider_set_value(volume_slider, 0, LV_ANIM_OFF);
    lv_obj_send_event(volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(test_volume == 0);
    lv_slider_set_value(volume_slider, 100, LV_ANIM_OFF);
    lv_obj_send_event(volume_slider, LV_EVENT_VALUE_CHANGED, NULL);
    assert(test_volume == 100);

    lv_obj_send_event(brightness_button, LV_EVENT_CLICKED, NULL);
    press_hardware();
    assert(!test_requested && test_state.phase == APP_STOPPING && request_calls == 1);
    assert(visible(brightness_sheet));
    close_brightness(NULL);
    press_hardware();
    assert(request_calls == 1);
    test_state.phase = APP_IDLE;
    snapshot("stopped");
    press_hardware();
    assert(test_requested && test_state.phase == APP_STARTING && request_calls == 2);
    press_hardware();
    assert(request_calls == 2);
    test_state.phase = APP_RUNNING;
    lv_obj_send_event(action, LV_EVENT_CLICKED, NULL);
    assert(!test_requested && request_calls == 3);
    test_state.phase = APP_ERROR;
    strcpy(test_state.message, "Connection failed. Press Retry to reconnect.");
    snapshot("error");
    assert(bulb_color == COLOR_RED);
    lv_obj_send_event(action, LV_EVENT_CLICKED, NULL);
    assert(test_requested && request_calls == 4);

    test_state.phase = APP_RUNNING;
    test_network.scan_generation++;
    ui_show_wifi_settings();
    step(100);
    assert(visible(wifi_page));
    snapshot("wifi");
    close_wifi(NULL);
    step(100);
    assert(!visible(wifi_page));
    ui_show_wifi_settings();
    step(100);
    lv_obj_send_event(lv_obj_get_child(ap_list, 0), LV_EVENT_CLICKED, NULL);
    assert(visible(password_page));
    lv_textarea_set_text(password_input, "short");
    connect_selected();
    assert(!connect_calls && visible(password_page));
    snapshot("password");
    lv_textarea_set_text(password_input, "test-password");
    connect_selected();
    assert(connect_calls == 1 && !lv_textarea_get_text(password_input)[0]);
    test_network.connection = NETWORK_CONNECTED;
    step(100);
    assert(!visible(wifi_page) && !visible(password_page));
    assert(!wifi_connect_pending);

    /* A network page opened for inspection stays open after a successful scan. */
    ui_show_wifi_settings();
    test_network.scan_generation++;
    test_ap_count = NETWORK_MAX_APS;
    step(100);
    assert(visible(wifi_page));
    assert(lv_obj_get_child_count(ap_list) == NETWORK_MAX_APS);
    lv_obj_scroll_to_y(ap_list, 900, LV_ANIM_OFF);
    step(100);
    for (unsigned i = 0; i < 5; ++i) {
        test_network.scan_generation++;
        step(100);
    }
    close_wifi(NULL);
    test_network.connection = NETWORK_FAILED;
    step(100);
    assert(visible(wifi_page) && !visible(action));
    test_network.connection = NETWORK_CONNECTED;
    close_wifi(NULL);
    /* An open AP starts connecting without showing the password page. */
    ui_show_wifi_settings();
    step(100);
    lv_obj_send_event(lv_obj_get_child(ap_list, 1), LV_EVENT_CLICKED, NULL);
    assert(connect_calls == 2 && !visible(password_page));
    test_network.connection = NETWORK_CONNECTED;
    step(100);
    test_state.pairing_ready = false;
    test_state.phase = APP_ERROR;
    press_hardware();
    assert(retry_calls == 1);

    test_time = 1793289600; /* 2026-10-29 16:00:00 UTC -> next day in Beijing. */
    step(100);
    assert(!strcmp(lv_label_get_text(clock_label), "00:00"));
    assert(!strcmp(lv_label_get_text(date_label), "Fri, 30 Oct 2026"));

    /* Refresh for several minutes, including every animated color. */
    test_state.pairing_ready = true;
    test_state.phase = APP_RUNNING;
    for (unsigned i = 0; i < 2400; ++i) {
        strcpy(test_state.agent_state, i % 2 ? "thinking" : "speaking");
        test_time++;
        step(100);
    }
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    printf("PASS: real LVGL rendering, pairing/auto-start, centered bulb/colors, screen/AI button, "
           "volume/brightness, battery states, Wi-Fi/password flow, messages and long rendering loop.\n");
    printf("LVGL pool: %zu bytes free of %zu (peak used %zu).\n",
           memory.free_size, memory.total_size, memory.max_used);
    lv_deinit();
    return 0;
}
