#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/*
 * ES8311 capture is interleaved [microphone, DAC reference] at 48 kHz.
 * Both channels pass through one stereo resampler before ESP-SR AEC at 16 kHz.
 */
#define AEC_CAPTURE_FRAMES_48K 480U
#define AEC_CAPTURE_CHANNELS 2U

esp_err_t aec_processor_init(void);
esp_err_t aec_processor_reset(void);
esp_err_t aec_processor_feed_capture_48k(const int16_t *interleaved, size_t frames);
/* Returns available processed samples; the caller reads more I2S data if short. */
size_t aec_processor_read_microphone_48k(int16_t *samples, size_t count);

