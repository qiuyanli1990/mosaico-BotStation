#include "device_power.h"

#include "bsp/esp_mosaico.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#define TAG "DEVICE_POWER"
#define BATTERY_POLL_US (5LL * 1000 * 1000)
#define BRIGHTNESS_SAVE_DELAY_US (1000LL * 1000)
#define BRIGHTNESS_RETRY_US (5LL * 1000 * 1000)

/* BQ27220 standard commands and BatteryStatus bits, matching the BSP driver. */
enum {
    GAUGE_VOLTAGE = 0x08,
    GAUGE_STATUS = 0x0A,
    GAUGE_CURRENT = 0x0C,
    GAUGE_SOC = 0x2C,
    GAUGE_DISCHARGING = 1U << 0,
    GAUGE_PRESENT = 1U << 3,
    GAUGE_CHARGE_INHIBIT = 1U << 8,
    GAUGE_FULL = 1U << 9,
};

static portMUX_TYPE power_lock = portMUX_INITIALIZER_UNLOCKED;
static device_power_status_t power_status = {.brightness = DEVICE_BRIGHTNESS_DEFAULT};
static i2c_master_dev_handle_t gauge;
static TaskHandle_t power_task_handle;
static unsigned brightness_generation;
static unsigned saved_generation;
static int64_t brightness_changed_us;
static int64_t save_retry_us;

void device_power_get_status(device_power_status_t *status)
{
    portENTER_CRITICAL(&power_lock);
    *status = power_status;
    portEXIT_CRITICAL(&power_lock);
}

esp_err_t device_power_set_brightness(unsigned percent)
{
    if (percent < DEVICE_BRIGHTNESS_MIN || percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = bsp_display_brightness_set((int)percent);
    if (result != ESP_OK) {
        return result;
    }
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&power_lock);
    if (!power_status.brightness_available || power_status.brightness != percent) {
        power_status.brightness = percent;
        brightness_generation++;
        brightness_changed_us = now;
    }
    power_status.brightness_available = true;
    portEXIT_CRITICAL(&power_lock);
    return ESP_OK;
}

static esp_err_t attach_gauge(void)
{
    if (gauge) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = i2c_master_probe(bus, BSP_BATTERY_I2C_ADDR, 50);
    if (result != ESP_OK) {
        return result;
    }
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_BATTERY_I2C_ADDR,
        .scl_speed_hz = BSP_BATTERY_I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &config, &gauge);
}

static esp_err_t read_gauge_word(uint8_t command, uint16_t *value)
{
    uint8_t bytes[2];
    esp_err_t result = i2c_master_transmit_receive(gauge, &command, 1, bytes, 2, 50);
    if (result == ESP_OK) {
        *value = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    }
    return result;
}

static void poll_battery(void)
{
    device_power_status_t sample = {0};
    uint16_t voltage = 0, flags = 0, current = 0, percent = 0;
    /* Read only standard registers: displaying charge must not reprogram the
     * fuel gauge's learned capacity, calibration, or battery profile. */
    if (attach_gauge() == ESP_OK &&
        read_gauge_word(GAUGE_STATUS, &flags) == ESP_OK &&
        read_gauge_word(GAUGE_VOLTAGE, &voltage) == ESP_OK &&
        read_gauge_word(GAUGE_CURRENT, &current) == ESP_OK &&
        read_gauge_word(GAUGE_SOC, &percent) == ESP_OK) {
        if (!(flags & GAUGE_PRESENT)) {
            sample.battery = DEVICE_BATTERY_ABSENT;
        } else if (percent <= 100 && voltage >= 2000 && voltage <= 5000) {
            sample.battery = DEVICE_BATTERY_AVAILABLE;
            sample.battery_percent = percent;
            sample.full = (flags & GAUGE_FULL) != 0;
            sample.charging = (int16_t)current > 2 &&
                              !(flags & (GAUGE_DISCHARGING | GAUGE_CHARGE_INHIBIT | GAUGE_FULL));
        }
    }
    portENTER_CRITICAL(&power_lock);
    power_status.battery = sample.battery;
    power_status.battery_percent = sample.battery_percent;
    power_status.charging = sample.charging;
    power_status.full = sample.full;
    portEXIT_CRITICAL(&power_lock);
}

static void save_brightness(int64_t now)
{
    portENTER_CRITICAL(&power_lock);
    const unsigned generation = brightness_generation;
    const unsigned percent = power_status.brightness;
    const int64_t changed = brightness_changed_us;
    portEXIT_CRITICAL(&power_lock);
    if (generation == saved_generation || now - changed < BRIGHTNESS_SAVE_DELAY_US ||
        now < save_retry_us) {
        return;
    }
    nvs_handle_t handle;
    esp_err_t result = nvs_open("display", NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_u8(handle, "brightness", (uint8_t)percent);
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (result == ESP_OK) {
        saved_generation = generation;
    } else {
        save_retry_us = now + BRIGHTNESS_RETRY_US;
        ESP_LOGW(TAG, "Brightness preference not saved: %s", esp_err_to_name(result));
    }
}

static void power_task(void *argument)
{
    (void)argument;
    int64_t next_battery_us = 0;
    for (;;) {
        const int64_t now = esp_timer_get_time();
        if (now >= next_battery_us) {
            poll_battery();
            next_battery_us = now + BATTERY_POLL_US;
        }
        save_brightness(now);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t device_power_init(void)
{
    if (power_task_handle) {
        return ESP_OK;
    }
    uint8_t brightness = DEVICE_BRIGHTNESS_DEFAULT;
    nvs_handle_t handle;
    if (nvs_open("display", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t saved;
        if (nvs_get_u8(handle, "brightness", &saved) == ESP_OK &&
            saved >= DEVICE_BRIGHTNESS_MIN && saved <= 100) {
            brightness = saved;
        }
        nvs_close(handle);
    }
    if (bsp_display_lock(1000)) {
        device_power_status_t current;
        device_power_get_status(&current);
        if (!current.brightness_available) {
            esp_err_t result = bsp_display_brightness_set(brightness);
            if (result == ESP_OK) {
                portENTER_CRITICAL(&power_lock);
                power_status.brightness = brightness;
                power_status.brightness_available = true;
                portEXIT_CRITICAL(&power_lock);
            } else {
                ESP_LOGW(TAG, "Could not apply startup brightness: %s", esp_err_to_name(result));
            }
        }
        bsp_display_unlock();
    }
    if (xTaskCreate(power_task, "device_power", 3072, NULL, 1, &power_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
