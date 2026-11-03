#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"
#include "driver/i2c_master.h"

typedef void *button_handle_t;
typedef void (*button_cb_t)(void *, void *);
typedef enum { BUTTON_SINGLE_CLICK } button_event_t;
enum { BSP_BUTTON_AI, BSP_BUTTON_NUM };
#define BSP_BUTTON_AI_GPIO 7
#define BSP_BATTERY_I2C_ADDR 0x55
#define BSP_BATTERY_I2C_SPEED_HZ 400000

lv_display_t *bsp_display_start(void);
bool bsp_display_lock(int timeout_ms);
void bsp_display_unlock(void);
esp_err_t bsp_display_brightness_set(int percent);
i2c_master_bus_handle_t bsp_i2c_get_handle(void);
esp_err_t bsp_iot_button_create(button_handle_t buttons[], int *count, int size);
esp_err_t iot_button_register_cb(button_handle_t button, button_event_t event,
                                void *args, button_cb_t callback, void *user_data);
esp_err_t iot_button_delete(button_handle_t button);
