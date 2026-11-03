#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "media.h"

int start_webrtc(const char *url, const char *token);
void query_webrtc(void);
bool webrtc_needs_restart(void);
bool webrtc_is_connected(void);
int stop_webrtc(void);

