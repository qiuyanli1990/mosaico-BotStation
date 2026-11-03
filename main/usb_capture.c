#include "usb_capture.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_audio.h"
#include "aec_processor.h"

static const char *TAG = "USB_CAPTURE";

static atomic_bool started[2];
static uint64_t samples_read[2];
static int64_t next_frame_us[2];
static esp_codec_dev_handle_t microphone;
static bool microphone_open;
/* esp_codec_dev_read copies from the I2S driver's internal DMA buffers. */
static EXT_RAM_BSS_ATTR int16_t microphone_frame[AEC_CAPTURE_FRAMES_48K * AEC_CAPTURE_CHANNELS];

void usb_capture_set_microphone(esp_codec_dev_handle_t handle)
{
    microphone = handle;
}

static bool open_microphone(void)
{
    if (!microphone) {
        return false;
    }
    esp_codec_dev_sample_info_t info = {
        .sample_rate = 48000,
        .bits_per_sample = 16,
        /* ES8311 REG44=0x58 supplies ADC-left and DAC-reference-right. */
        .channel = AEC_CAPTURE_CHANNELS,
        .channel_mask = 0,
    };
    int result = esp_codec_dev_open(microphone, &info);
    if (result != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(microphone);
        ESP_LOGE(TAG, "Open onboard microphone failed: %d", result);
        return false;
    }
    int reference_config = 0;
    result = esp_codec_dev_read_reg(microphone, 0x44, &reference_config);
    if (result != ESP_CODEC_DEV_OK || (reference_config & 0x70) != 0x50) {
        ESP_LOGE(TAG, "ES8311 DAC reference is unavailable: REG44=0x%02x, error=%d",
                 reference_config, result);
        esp_codec_dev_close(microphone);
        return false;
    }
    result = esp_codec_dev_set_in_gain(microphone, 30.0f);
    if (result != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Set onboard microphone gain failed: %d", result);
    }
    microphone_open = true;
    ESP_LOGI(TAG, "Onboard microphone opened: 48 kHz, 16-bit, "
             "stereo [ADC mic, DAC reference], gain 30 dB");
    return true;
}

static esp_capture_err_t source_open(esp_capture_audio_src_if_t *src)
{
    (void)src;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t source_codecs(esp_capture_audio_src_if_t *src,
                                     const esp_capture_format_id_t **codecs, uint8_t *count)
{
    (void)src;
    static const esp_capture_format_id_t pcm = ESP_CAPTURE_FMT_ID_PCM;
    *codecs = &pcm;
    *count = 1;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t source_caps(esp_capture_audio_src_if_t *src,
                                   esp_capture_audio_info_t *requested,
                                   esp_capture_audio_info_t *actual)
{
    (void)src;
    if (requested->format_id != ESP_CAPTURE_FMT_ID_PCM) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    *actual = (esp_capture_audio_info_t) {
        .format_id = ESP_CAPTURE_FMT_ID_PCM,
        .sample_rate = 48000,
        .channel = 1,
        .bits_per_sample = 16,
    };
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t source_fixed(esp_capture_audio_src_if_t *src,
                                    const esp_capture_audio_info_t *caps)
{
    (void)src;
    return caps->format_id == ESP_CAPTURE_FMT_ID_PCM && caps->sample_rate == 48000 &&
           caps->channel == 1 && caps->bits_per_sample == 16 ?
           ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static bool source_is_microphone(esp_capture_audio_src_if_t *src);

static esp_capture_err_t source_start(esp_capture_audio_src_if_t *src)
{
    unsigned index = source_is_microphone(src) ? 1 : 0;
    samples_read[index] = 0;
    next_frame_us[index] = esp_timer_get_time();
    if (index == 1) {
        if (aec_processor_reset() != ESP_OK) {
            ESP_LOGE(TAG, "Could not reset AEC processing");
            return ESP_CAPTURE_ERR_INTERNAL;
        }
        if (!open_microphone()) {
            return ESP_CAPTURE_ERR_NOT_SUPPORTED;
        }
        ESP_LOGI(TAG, "Capture source: onboard microphone with synchronized DAC-reference AEC");
    } else {
        ESP_LOGI(TAG, "Capture source: USB speaker audio");
    }
    atomic_store(&started[index], true);
    return ESP_CAPTURE_ERR_OK;
}

static bool source_is_microphone(esp_capture_audio_src_if_t *src)
{
    return src == usb_capture_source(USB_CAPTURE_SOURCE_MICROPHONE);
}

static esp_capture_err_t source_read(esp_capture_audio_src_if_t *src,
                                   esp_capture_stream_frame_t *frame)
{
    if (!frame || !frame->data || frame->size <= 0 || frame->size % 2) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    unsigned index = source_is_microphone(src) ? 1 : 0;
    size_t samples = frame->size / sizeof(int16_t);
    if (!atomic_load(&started[index])) return ESP_CAPTURE_ERR_INVALID_STATE;
    if (index == 1) {
        if (!microphone || !microphone_open) return ESP_CAPTURE_ERR_INVALID_STATE;
        size_t produced = 0;
        while (produced < samples && atomic_load(&started[index])) {
            produced += aec_processor_read_microphone_48k(
                (int16_t *)frame->data + produced, samples - produced);
            if (produced == samples) break;
            /* I2S paces microphone capture. A second software clock builds
             * backlog and lets the playback reference drift against it. */
            int result = esp_codec_dev_read(microphone, microphone_frame,
                                            sizeof(microphone_frame));
            if (result != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "Read microphone/DAC reference failed: %d", result);
                return ESP_CAPTURE_ERR_INTERNAL;
            }
            esp_err_t aec_result = aec_processor_feed_capture_48k(
                microphone_frame, AEC_CAPTURE_FRAMES_48K);
            if (aec_result != ESP_OK) {
                ESP_LOGE(TAG, "Process synchronized AEC capture failed: %d", aec_result);
                return ESP_CAPTURE_ERR_INTERNAL;
            }
        }
        if (produced != samples) return ESP_CAPTURE_ERR_INVALID_STATE;
        frame->pts = samples_read[index] * 1000 / 48000;
        samples_read[index] += samples;
        return ESP_CAPTURE_ERR_OK;
    }
    int64_t duration_us = samples * 1000000ULL / 48000;
    next_frame_us[index] += duration_us;
    /* Keep the capture pipeline paced and avoid flooding Opus on USB underflow. */
    while (atomic_load(&started[index])) {
        int64_t remaining = next_frame_us[index] - esp_timer_get_time();
        if (remaining <= 0) {
            break;
        }
        uint32_t wait_ms = remaining / 1000;
        if (wait_ms > 10) {
            wait_ms = 10;
        }
        vTaskDelay(pdMS_TO_TICKS(wait_ms ? wait_ms : 1));
    }
    if (!atomic_load(&started[index])) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    if (!usb_audio_is_mounted()) {
        memset(frame->data, 0, samples * sizeof(int16_t));
    } else {
        size_t read = usb_audio_read((int16_t *)frame->data, samples);
        memset(frame->data + read * sizeof(int16_t), 0,
               (samples - read) * sizeof(int16_t));
    }
    if (esp_timer_get_time() - next_frame_us[index] > duration_us * 2) {
        next_frame_us[index] = esp_timer_get_time();
    }
    frame->pts = samples_read[index] * 1000 / 48000;
    samples_read[index] += samples;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t source_stop(esp_capture_audio_src_if_t *src)
{
    (void)src;
    unsigned index = source_is_microphone(src) ? 1 : 0;
    atomic_store(&started[index], false);
    if (index == 1 && microphone_open) {
        esp_codec_dev_close(microphone);
        microphone_open = false;
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_audio_src_if_t *usb_capture_source(usb_capture_source_id_t source_id)
{
    static esp_capture_audio_src_if_t sources[] = {
        {
            .open = source_open, .close = source_stop, .start = source_start, .stop = source_stop,
            .get_support_codecs = source_codecs, .set_fixed_caps = source_fixed,
            .negotiate_caps = source_caps, .read_frame = source_read,
        },
        {
            .open = source_open, .close = source_stop, .start = source_start, .stop = source_stop,
            .get_support_codecs = source_codecs, .set_fixed_caps = source_fixed,
            .negotiate_caps = source_caps, .read_frame = source_read,
        },
    };
    return source_id <= USB_CAPTURE_SOURCE_MICROPHONE ? &sources[source_id] : NULL;
}
