#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define DEVICE_BRIGHTNESS_MIN 10U
#define DEVICE_BRIGHTNESS_DEFAULT 70U

typedef enum {
    DEVICE_BATTERY_UNAVAILABLE,
    DEVICE_BATTERY_ABSENT,
    DEVICE_BATTERY_AVAILABLE,
} device_battery_state_t;

typedef struct {
    device_battery_state_t battery;
    unsigned battery_percent;
    bool charging;
    bool full;
    unsigned brightness;
    bool brightness_available;
} device_power_status_t;

/* Start after the BSP display/audio have initialized their shared buses. */
esp_err_t device_power_init(void);
void device_power_get_status(device_power_status_t *status);
/* Call from the LVGL task (or while holding the BSP display lock). */
esp_err_t device_power_set_brightness(unsigned percent);
