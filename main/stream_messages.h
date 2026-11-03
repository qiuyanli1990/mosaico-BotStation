#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_webrtc.h"

esp_err_t stream_messages_init(void);
void stream_messages_clear(void);
int stream_messages_on_custom_data(esp_webrtc_custom_data_via_t via,
                                   uint8_t *data, int size, void *ctx);
