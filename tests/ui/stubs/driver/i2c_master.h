#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef void *i2c_master_bus_handle_t;
typedef void *i2c_master_dev_handle_t;
enum { I2C_ADDR_BIT_LEN_7 };
typedef struct {
    int dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
} i2c_device_config_t;

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout_ms);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                   const i2c_device_config_t *config, i2c_master_dev_handle_t *device);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                     const void *transmit, size_t transmit_size,
                                     void *receive, size_t receive_size, int timeout_ms);
