#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Use the actual service with checked I2C/NVS/FreeRTOS boundary mocks. */
#include "../../main/device_power.c"

static int64_t test_now;
static uint16_t register_flags = GAUGE_PRESENT | GAUGE_DISCHARGING;
static uint16_t register_voltage = 3900;
static uint16_t register_current = (uint16_t)-20;
static uint16_t register_soc = 78;
static int failed_command = -1;
static bool probe_failed;
static bool add_failed;
static bool display_failed;
static bool display_lock_failed;
static bool task_failed;
static bool nvs_failed;
static bool nvs_has_value;
static uint8_t nvs_value;
static uint8_t pending_nvs;
static unsigned commits;
static unsigned writes;
static unsigned display_calls;
static unsigned task_calls;
static unsigned last_brightness;

void host_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}

const char *esp_err_to_name(esp_err_t error) { return error == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }
int64_t esp_timer_get_time(void) { return test_now; }
i2c_master_bus_handle_t bsp_i2c_get_handle(void) { return (void *)1; }
bool bsp_display_lock(int timeout_ms) { assert(timeout_ms == 1000); return !display_lock_failed; }
void bsp_display_unlock(void) {}
esp_err_t bsp_display_brightness_set(int percent)
{
    assert(percent >= 10 && percent <= 100);
    display_calls++;
    if (display_failed) {
        return ESP_FAIL;
    }
    last_brightness = (unsigned)percent;
    return ESP_OK;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout_ms)
{
    assert(bus == (void *)1 && address == 0x55 && timeout_ms == 50);
    return probe_failed ? ESP_FAIL : ESP_OK;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                   const i2c_device_config_t *config, i2c_master_dev_handle_t *device)
{
    assert(bus == (void *)1 && config->device_address == 0x55);
    assert(config->scl_speed_hz == 400000 && config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    if (add_failed) {
        return ESP_FAIL;
    }
    *device = (void *)2;
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
                                     const void *transmit, size_t transmit_size,
                                     void *receive, size_t receive_size, int timeout_ms)
{
    /* Reading status must never send a profile, calibration, or control write. */
    assert(device == (void *)2 && transmit_size == 1 && receive_size == 2 && timeout_ms == 50);
    const uint8_t command = *(const uint8_t *)transmit;
    if (command == failed_command) {
        return ESP_FAIL;
    }
    uint16_t value;
    switch (command) {
    case 0x08: value = register_voltage; break;
    case 0x0A: value = register_flags; break;
    case 0x0C: value = register_current; break;
    case 0x2C: value = register_soc; break;
    default: assert(!"Unexpected gauge write/read"); return ESP_FAIL;
    }
    ((uint8_t *)receive)[0] = value & 0xFF;
    ((uint8_t *)receive)[1] = value >> 8;
    return ESP_OK;
}

BaseType_t xTaskCreate(TaskFunction_t task, const char *name, uint32_t stack_size,
                      void *argument, UBaseType_t priority, TaskHandle_t *handle)
{
    assert(task == power_task && !strcmp(name, "device_power"));
    assert(stack_size == 3072 && priority == 1 && argument == NULL);
    if (task_failed) {
        return 0;
    }
    task_calls++;
    *handle = (void *)3;
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) { (void)ticks; assert(!"Infinite task not used in host test"); }

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(name, "display"));
    if (nvs_failed || (mode == NVS_READONLY && !nvs_has_value)) {
        return ESP_FAIL;
    }
    *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    assert(handle == 1 && !strcmp(key, "brightness"));
    if (!nvs_has_value) {
        return ESP_FAIL;
    }
    *value = nvs_value;
    return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    assert(handle == 1 && !strcmp(key, "brightness"));
    assert(value >= 10 && value <= 100);
    writes++;
    pending_nvs = value;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1);
    commits++;
    nvs_value = pending_nvs;
    nvs_has_value = true;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1); }

static void reset_service(void)
{
    power_status = (device_power_status_t){.brightness = DEVICE_BRIGHTNESS_DEFAULT};
    power_task_handle = NULL;
    gauge = NULL;
    brightness_generation = saved_generation = 0;
    brightness_changed_us = save_retry_us = 0;
    display_failed = display_lock_failed = task_failed = nvs_failed = false;
    display_calls = task_calls = writes = commits = 0;
    test_now = 0;
}

int main(void)
{
    assert(device_power_init() == ESP_OK);
    assert(last_brightness == 70 && !commits && !writes && task_calls == 1);
    assert(power_status.brightness_available);
    assert(device_power_init() == ESP_OK && task_calls == 1);
    poll_battery();
    device_power_status_t snapshot;
    device_power_get_status(&snapshot);
    assert(snapshot.battery == DEVICE_BATTERY_AVAILABLE && snapshot.battery_percent == 78);
    assert(!snapshot.charging && !snapshot.full);

    register_flags = GAUGE_PRESENT;
    register_current = 20;
    poll_battery();
    assert(power_status.charging);
    register_flags |= GAUGE_FULL;
    register_soc = 100;
    poll_battery();
    assert(power_status.full && !power_status.charging);
    register_flags = GAUGE_PRESENT | GAUGE_CHARGE_INHIBIT;
    poll_battery();
    assert(!power_status.charging);
    register_flags = GAUGE_PRESENT | GAUGE_DISCHARGING;
    register_soc = 0;
    poll_battery();
    assert(power_status.battery == DEVICE_BATTERY_AVAILABLE && power_status.battery_percent == 0);
    register_flags = 0;
    poll_battery();
    assert(power_status.battery == DEVICE_BATTERY_ABSENT && !power_status.charging);

    register_flags = GAUGE_PRESENT;
    register_soc = 101;
    poll_battery();
    assert(power_status.battery == DEVICE_BATTERY_UNAVAILABLE);
    register_soc = 78;
    register_voltage = 65535;
    poll_battery();
    assert(power_status.battery == DEVICE_BATTERY_UNAVAILABLE);
    register_voltage = 3900;
    const int commands[] = {0x08, 0x0A, 0x0C, 0x2C};
    for (unsigned i = 0; i < 4; ++i) {
        failed_command = -1;
        poll_battery();
        assert(power_status.battery == DEVICE_BATTERY_AVAILABLE);
        failed_command = commands[i];
        poll_battery();
        assert(power_status.battery == DEVICE_BATTERY_UNAVAILABLE);
        assert(!power_status.charging && power_status.battery_percent == 0);
    }
    failed_command = -1;
    gauge = NULL;
    probe_failed = true;
    poll_battery();
    assert(power_status.battery == DEVICE_BATTERY_UNAVAILABLE && gauge == NULL);
    probe_failed = false;
    add_failed = true;
    poll_battery();
    assert(power_status.battery == DEVICE_BATTERY_UNAVAILABLE && gauge == NULL);
    add_failed = false;
    poll_battery();
    assert(power_status.battery == DEVICE_BATTERY_AVAILABLE);

    test_now = 100;
    assert(device_power_set_brightness(20) == ESP_OK);
    save_brightness(900000);
    assert(!writes);
    test_now = 900000;
    assert(device_power_set_brightness(60) == ESP_OK);
    save_brightness(1100000);
    assert(!writes);
    save_brightness(1900000);
    assert(writes == 1 && commits == 1 && nvs_value == 60);
    save_brightness(2000000);
    assert(commits == 1);
    test_now = 2000000;
    assert(device_power_set_brightness(80) == ESP_OK);
    nvs_failed = true;
    save_brightness(3000000);
    assert(power_status.brightness == 80 && commits == 1);
    nvs_failed = false;
    save_brightness(4000000);
    assert(commits == 1);
    save_brightness(8000000);
    assert(commits == 2 && nvs_value == 80);
    unsigned previous_calls = display_calls;
    assert(device_power_set_brightness(0) == ESP_ERR_INVALID_ARG);
    assert(device_power_set_brightness(101) == ESP_ERR_INVALID_ARG);
    assert(display_calls == previous_calls);
    display_failed = true;
    assert(device_power_set_brightness(90) == ESP_FAIL && power_status.brightness == 80);
    display_failed = false;
    assert(device_power_set_brightness(10) == ESP_OK && power_status.brightness == 10);

    reset_service();
    nvs_has_value = true;
    nvs_value = 31;
    assert(device_power_init() == ESP_OK && power_status.brightness == 31 && last_brightness == 31);
    assert(!writes && !commits);
    reset_service();
    nvs_value = 0;
    assert(device_power_init() == ESP_OK && power_status.brightness == 70);
    reset_service();
    nvs_value = 31;
    assert(device_power_set_brightness(45) == ESP_OK);
    assert(device_power_init() == ESP_OK && power_status.brightness == 45);
    reset_service();
    display_failed = true;
    assert(device_power_init() == ESP_OK && !power_status.brightness_available);
    reset_service();
    display_lock_failed = true;
    assert(device_power_init() == ESP_OK && !power_status.brightness_available);
    reset_service();
    task_failed = true;
    assert(device_power_init() == ESP_ERR_NO_MEM);
    assert(device_power_set_brightness(50) == ESP_OK);

    puts("PASS: checked read-only gauge sampling, charging/full/absent/unknown, I2C recovery, "
         "brightness limits/failures, saved preference restore, debounce and storage retry.");
    return 0;
}
