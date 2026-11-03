#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SUBTITLE_TEXT_CAPACITY 256U

void subtitles_clear(void);
void subtitles_handle_data(const uint8_t *data, size_t length);
uint32_t subtitles_generation(void);
bool subtitles_copy(char *out, size_t capacity);
