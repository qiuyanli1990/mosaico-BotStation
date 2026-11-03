#include "app_state.h"
#include "bot_station.h"
#include "device_power.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "media.h"
#include "network.h"
#include "nvs_flash.h"
#include "session.h"
#include "ui.h"
#include "usb_audio.h"
#include "usb_console.h"

static void log_heap(const char *stage)
{
    ESP_LOGI("APP", "%s: internal free=%u largest=%u, DMA free=%u largest=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
}

void app_main(void)
{
    usb_console_init();
    esp_err_t result = nvs_flash_init();
    if (result != ESP_OK) {
        ESP_LOGE("APP", "NVS init failed (%s); existing NVS was not erased", esp_err_to_name(result));
        return;
    }

    log_heap("Startup heap");
    bool degraded = false;
    result = ui_init();
    bool display_ready = result == ESP_OK;
    if (result != ESP_OK) {
        ESP_LOGE("APP", "Display initialization failed: %s", esp_err_to_name(result));
        degraded = true;
    }

    result = media_init();
    if (result != ESP_OK) {
        ESP_LOGE("APP", "Media/AEC initialization failed: %s", esp_err_to_name(result));
        degraded = true;
    }
    result = usb_audio_init();
    if (result != ESP_OK) {
        ESP_LOGE("APP", "USB audio initialization failed: %s", esp_err_to_name(result));
        degraded = true;
    }

    /* Hardware DMA resources are initialized before Wi-Fi's RX/TX buffers.
     * A module failure still allows Wi-Fi provisioning to start. */
    log_heap("Pre-network heap");
    ESP_LOGI("APP", "Starting Wi-Fi after display, media, and USB initialization");
    result = network_start();
    bool network_ready = result == ESP_OK;
    if (!network_ready) {
        ESP_LOGE("APP", "Wi-Fi initialization failed: %s", esp_err_to_name(result));
        app_set_status(APP_ERROR, "Wi-Fi initialization failed");
        degraded = true;
    } else if (!network_has_credentials()) {
        app_set_status(APP_WAITING_FOR_WIFI, "Waiting for Wi-Fi setup");
        if (display_ready) {
            ui_show_wifi_settings();
        } else {
            ESP_LOGW("APP", "Wi-Fi has no saved credentials; display is unavailable");
        }
    }

    bool bot_station_ready = false;
    result = bot_station_init();
    if (result != ESP_OK) {
        ESP_LOGE("APP", "MyBot Device API initialization failed: %s", esp_err_to_name(result));
        degraded = true;
    } else {
        bot_station_ready = true;
    }
    if (bot_station_ready) {
        result = session_init();
        if (result != ESP_OK) {
            ESP_LOGE("APP", "MyBot session initialization failed: %s", esp_err_to_name(result));
            app_set_status(APP_ERROR, "MyBot session initialization failed");
            degraded = true;
        }
    }
    if (display_ready) {
        result = device_power_init();
        if (result != ESP_OK) {
            ESP_LOGW("APP", "Power status monitor unavailable: %s", esp_err_to_name(result));
        }
    }
    log_heap("Startup complete heap");

    if (degraded && network_ready && !bot_station_ready) {
        app_set_status(APP_ERROR, "Wi-Fi is ready; check UART for a module error");
    }
}
