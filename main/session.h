#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

esp_err_t session_init(void);
void session_request(bool start);
bool session_requested(void);
bool session_auto_start_pending(void);
void session_set_languages(size_t source_index, size_t target_index);

