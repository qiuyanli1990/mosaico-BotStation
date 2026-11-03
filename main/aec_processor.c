#include "aec_processor.h"

#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_aec.h"
#include "esp_ae_rate_cvt.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pcm_ring.h"
#include "sdkconfig.h"

#define AEC_SAMPLE_RATE 16000
#define AEC_MAX_CHUNK 512
#define AEC_OUTPUT_CAPACITY (AEC_MAX_CHUNK * 3 * 4)
#define AEC_LOG_INTERVAL_US (5LL * 1000 * 1000)

static const char *TAG = "AEC";
static aec_handle_t *aec;
static int aec_chunk;
static SemaphoreHandle_t lock;
static esp_ae_rate_cvt_handle_t capture_resampler;
static esp_ae_rate_cvt_handle_t output_resampler;
static int16_t *capture_16k;
static int16_t *output_48k;
static int16_t *mic_frame;
static int16_t *ref_frame;
static int16_t *clean_frame;
static uint32_t capture_capacity;
static uint32_t output_capacity;
static size_t pending_samples;
static bool ready;
static EXT_RAM_BSS_ATTR int16_t output_buffer[AEC_OUTPUT_CAPACITY];
static pcm_ring_t output = {.samples = output_buffer, .capacity = AEC_OUTPUT_CAPACITY};

typedef struct {
    uint64_t mic_energy;
    uint64_t ref_energy;
    uint64_t out_energy;
    uint32_t samples;
    uint32_t frames;
    uint32_t reference_frames;
    uint32_t clipped_samples;
    int64_t max_process_us;
    int64_t last_log_us;
} aec_metrics_t;

static aec_metrics_t metrics;

static void release_resources(void)
{
    if (aec) aec_destroy(aec);
    if (capture_resampler) esp_ae_rate_cvt_close(capture_resampler);
    if (output_resampler) esp_ae_rate_cvt_close(output_resampler);
    free(capture_16k);
    free(output_48k);
    free(mic_frame);
    free(ref_frame);
    free(clean_frame);
    aec = NULL;
    capture_resampler = NULL;
    output_resampler = NULL;
    capture_16k = output_48k = mic_frame = ref_frame = clean_frame = NULL;
}

static esp_err_t create_aec(void)
{
    aec = aec_create(AEC_SAMPLE_RATE, 4, 1, AEC_MODE_FD_HIGH_PERF);
    if (!aec) return ESP_ERR_NO_MEM;
    aec_chunk = aec_get_chunksize(aec);
    if (aec_chunk <= 0 || aec_chunk > AEC_MAX_CHUNK) {
        ESP_LOGE(TAG, "Unsupported AEC frame size: %d", aec_chunk);
        aec_destroy(aec);
        aec = NULL;
        return ESP_ERR_NOT_SUPPORTED;
    }
    /* Full-duplex NLP suppresses residual echo while retaining near-end speech. */
    aec_set_nlp_level(aec, AEC_NLP_LEVEL_AGGR);
    return ESP_OK;
}

static int16_t *allocate_samples(size_t count)
{
    return heap_caps_aligned_calloc(16, count, sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

esp_err_t aec_processor_init(void)
{
    if (lock) return ESP_OK;
    esp_err_t result = create_aec();
    if (result != ESP_OK) return result;

    esp_ae_rate_cvt_cfg_t down = {
        .src_rate = 48000, .dest_rate = AEC_SAMPLE_RATE,
        .channel = AEC_CAPTURE_CHANNELS, .bits_per_sample = 16,
        .complexity = 2, .perf_type = ESP_AE_RATE_CVT_PERF_TYPE_MEMORY,
    };
    esp_ae_rate_cvt_cfg_t up = down;
    up.src_rate = AEC_SAMPLE_RATE;
    up.dest_rate = 48000;
    up.channel = 1;
    if (esp_ae_rate_cvt_open(&down, &capture_resampler) != ESP_AE_ERR_OK ||
        esp_ae_rate_cvt_open(&up, &output_resampler) != ESP_AE_ERR_OK ||
        esp_ae_rate_cvt_get_max_out_sample_num(capture_resampler,
            AEC_CAPTURE_FRAMES_48K, &capture_capacity) != ESP_AE_ERR_OK ||
        esp_ae_rate_cvt_get_max_out_sample_num(output_resampler,
            (uint32_t)aec_chunk, &output_capacity) != ESP_AE_ERR_OK ||
        !capture_capacity || !output_capacity || output_capacity > AEC_OUTPUT_CAPACITY) {
        release_resources();
        return ESP_FAIL;
    }
    capture_16k = allocate_samples(capture_capacity * AEC_CAPTURE_CHANNELS);
    output_48k = allocate_samples(output_capacity);
    mic_frame = allocate_samples((size_t)aec_chunk);
    ref_frame = allocate_samples((size_t)aec_chunk);
    clean_frame = allocate_samples((size_t)aec_chunk);
    if (!capture_16k || !output_48k || !mic_frame || !ref_frame || !clean_frame) {
        release_resources();
        return ESP_ERR_NO_MEM;
    }
    lock = xSemaphoreCreateMutex();
    if (!lock) {
        release_resources();
        return ESP_ERR_NO_MEM;
    }
    ready = true;
    ESP_LOGI(TAG, "AEC configured: ES8311 ADC-left/DAC-ref-right, 48k stereo -> "
             "16k paired -> %s -> 48k mono; chunk=%d, NLP=AGGR",
             aec_get_mode_string(AEC_MODE_FD_HIGH_PERF), aec_chunk);
#if CONFIG_MYBOT_AEC_BYPASS
    ESP_LOGW(TAG, "AEC BYPASS enabled for A/B measurement; echo is not cancelled");
#endif
    return ESP_OK;
}

esp_err_t aec_processor_reset(void)
{
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    pcm_ring_reset(&output);
    output.dropped = 0;
    pending_samples = 0;
    ready = false;
    memset(&metrics, 0, sizeof(metrics));
    metrics.last_log_us = esp_timer_get_time();
    /* Reset filter history as well as queues between conversations. */
    if (aec) aec_destroy(aec);
    aec = NULL;
    int previous_chunk = aec_chunk;
    esp_err_t result = create_aec();
    if (result == ESP_OK && aec_chunk != previous_chunk) {
        aec_destroy(aec);
        aec = NULL;
        result = ESP_ERR_NOT_SUPPORTED;
    }
    /* The buffers were sized at init. Keep that size even after a failed reset. */
    aec_chunk = previous_chunk;
    esp_ae_err_t capture_result = esp_ae_rate_cvt_reset(capture_resampler);
    esp_ae_err_t output_result = esp_ae_rate_cvt_reset(output_resampler);
    if (capture_result != ESP_AE_ERR_OK || output_result != ESP_AE_ERR_OK) {
        result = ESP_FAIL;
    }
    ready = result == ESP_OK;
    xSemaphoreGive(lock);
    return result;
}

static void update_metrics(int64_t process_us)
{
    uint64_t frame_ref_energy = 0;
    for (int i = 0; i < aec_chunk; ++i) {
        int32_t mic = mic_frame[i];
        int32_t ref = ref_frame[i];
        int32_t clean = clean_frame[i];
        metrics.mic_energy += (uint64_t)(mic * mic);
        frame_ref_energy += (uint64_t)(ref * ref);
        metrics.out_energy += (uint64_t)(clean * clean);
        if (mic >= 32000 || mic <= -32000) ++metrics.clipped_samples;
    }
    metrics.ref_energy += frame_ref_energy;
    metrics.samples += (uint32_t)aec_chunk;
    ++metrics.frames;
    if (frame_ref_energy > (uint64_t)aec_chunk * 16 * 16) ++metrics.reference_frames;
    if (process_us > metrics.max_process_us) metrics.max_process_us = process_us;
    int64_t now = esp_timer_get_time();
    if (now - metrics.last_log_us >= AEC_LOG_INTERVAL_US) {
        ESP_LOGI(TAG, "AEC stats: frames=%" PRIu32 " ref_active=%" PRIu32
                 " mic_rms=%.0f ref_rms=%.0f out_rms=%.0f mic_clip=%" PRIu32
                 " max_us=%lld queued_ms=%u",
                 metrics.frames, metrics.reference_frames,
                 sqrt((double)metrics.mic_energy / metrics.samples),
                 sqrt((double)metrics.ref_energy / metrics.samples),
                 sqrt((double)metrics.out_energy / metrics.samples),
                 metrics.clipped_samples, (long long)metrics.max_process_us,
                 (unsigned)(output.count / 48));
        memset(&metrics, 0, sizeof(metrics));
        metrics.last_log_us = now;
    }
}

esp_err_t aec_processor_feed_capture_48k(const int16_t *interleaved, size_t frames)
{
    if (!interleaved || !frames || frames > AEC_CAPTURE_FRAMES_48K) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!lock || xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!ready) {
        xSemaphoreGive(lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = ESP_OK;
    uint32_t converted = capture_capacity;
    if (!aec || esp_ae_rate_cvt_process(capture_resampler,
            (esp_ae_sample_t)interleaved, (uint32_t)frames,
            (esp_ae_sample_t)capture_16k, &converted) != ESP_AE_ERR_OK ||
        converted > capture_capacity) {
        result = ESP_FAIL;
        goto done;
    }
    for (uint32_t i = 0; i < converted; ++i) {
        mic_frame[pending_samples] = capture_16k[i * AEC_CAPTURE_CHANNELS];
        ref_frame[pending_samples] = capture_16k[i * AEC_CAPTURE_CHANNELS + 1];
        if (++pending_samples != (size_t)aec_chunk) continue;
        pending_samples = 0;
        int64_t started_us = esp_timer_get_time();
#if CONFIG_MYBOT_AEC_BYPASS
        memcpy(clean_frame, mic_frame, (size_t)aec_chunk * sizeof(int16_t));
#else
        aec_process(aec, mic_frame, ref_frame, clean_frame);
#endif
        uint32_t produced = output_capacity;
        if (esp_ae_rate_cvt_process(output_resampler, (esp_ae_sample_t)clean_frame,
                (uint32_t)aec_chunk, (esp_ae_sample_t)output_48k,
                &produced) != ESP_AE_ERR_OK || produced > output_capacity) {
            result = ESP_FAIL;
            goto done;
        }
        if (produced > output.capacity - output.count) {
            ESP_LOGE(TAG, "Processed audio queue overflow; capture consumer is behind");
            result = ESP_ERR_INVALID_STATE;
            goto done;
        }
        pcm_ring_write(&output, output_48k, produced);
        update_metrics(esp_timer_get_time() - started_us);
    }
done:
    if (result != ESP_OK) {
        /* A broken frame sequence needs a full reset, never a raw-mic fallback. */
        ready = false;
        pending_samples = 0;
        pcm_ring_reset(&output);
    }
    xSemaphoreGive(lock);
    return result;
}

size_t aec_processor_read_microphone_48k(int16_t *samples, size_t count)
{
    if (!samples || !count || !lock ||
        xSemaphoreTake(lock, portMAX_DELAY) != pdTRUE) return 0;
    /* Do not insert silence here: the I2S reader supplies the next real chunk. */
    size_t produced = ready ? pcm_ring_read(&output, samples, count) : 0;
    xSemaphoreGive(lock);
    return produced;
}
