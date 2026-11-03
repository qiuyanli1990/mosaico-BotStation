/*
 * Execute the actual aec_processor.c, usb_capture.c and pcm_ring.c on the host.
 * Only ESP/RTOS/codec APIs are mocked. The deterministic DSP stubs verify channel
 * routing, framing and error handling; they do not measure acoustic cancellation.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aec_processor.h"
#include "usb_capture.h"
#include "usb_audio.h"
#include "esp_aec.h"
#include "esp_ae_rate_cvt.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static int chunk_size = 256;
static int fail_step, init_steps, live_aec, live_resamplers, live_samples;
static void *sample_allocations[5];
static int fail_process_channel, oversize_channel, fail_reset_channel;
static int aec_calls, resampler_resets, delays, mic_reads, mic_closes;
static bool fail_aec_create, fail_mic_open, fail_mic_read;
static int reference_register = 0x58;
static size_t i2s_position;
static int64_t mock_time;

static bool init_failure(void) { return ++init_steps == fail_step; }
static int16_t mic_sample(size_t i) { return 5000 + (int)((i * 17) % 3000); }
static int16_t ref_sample(size_t i) { return 1000 + (int)((i * 31) % 2000); }
static int16_t expected_sample(size_t i)
{
    size_t input_index = (i / 3) * 3 + 2;
#if CONFIG_MYBOT_AEC_BYPASS
    return mic_sample(input_index);
#else
    return mic_sample(input_index) - ref_sample(input_index);
#endif
}

void host_log(const char *tag, const char *format, ...)
{
    /* Preserve type/argument evaluation without printing expected fault logs. */
    (void)tag;
    (void)format;
}
int64_t esp_timer_get_time(void) { return ++mock_time; }
void vTaskDelay(uint32_t ticks) { ++delays; mock_time += ticks * 1000; }
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    static int mutex;
    return init_failure() ? NULL : &mutex;
}
int xSemaphoreTake(SemaphoreHandle_t lock, uint32_t timeout)
{
    (void)timeout;
    assert(lock);
    return pdTRUE;
}
int xSemaphoreGive(SemaphoreHandle_t lock) { assert(lock); return pdTRUE; }
void *heap_caps_aligned_calloc(size_t alignment, size_t count, size_t size, uint32_t caps)
{
    assert(alignment == 16 && caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (init_failure()) return NULL;
    size_t bytes = count * size;
    void *memory = aligned_alloc(alignment, (bytes + alignment - 1) / alignment * alignment);
    assert(memory);
    memset(memory, 0, bytes);
    for (size_t i = 0; i < 5; ++i) {
        if (!sample_allocations[i]) {
            sample_allocations[i] = memory;
            ++live_samples;
            return memory;
        }
    }
    abort();
}
void host_free(void *memory)
{
    if (!memory) return;
    for (size_t i = 0; i < 5; ++i) {
        if (sample_allocations[i] == memory) {
            sample_allocations[i] = NULL;
            --live_samples;
            free(memory);
            return;
        }
    }
    abort();
}

aec_handle_t *aec_create(int rate, int length, int channels, aec_mode_t mode)
{
    assert(rate == 16000 && length == 4 && channels == 1 && mode == AEC_MODE_FD_HIGH_PERF);
    if (init_failure() || fail_aec_create) return NULL;
    aec_handle_t *handle = calloc(1, sizeof(*handle));
    assert(handle);
    handle->frame_size = chunk_size;
    ++live_aec;
    return handle;
}
void aec_destroy(aec_handle_t *handle) { assert(handle); --live_aec; free(handle); }
int aec_get_chunksize(const aec_handle_t *handle) { return handle->frame_size; }
char *aec_get_mode_string(aec_mode_t mode) { (void)mode; return "FD_HIGH_PERF"; }
aec_nlp_level_t aec_set_nlp_level(const aec_handle_t *handle, aec_nlp_level_t level)
{
    assert(handle && level == AEC_NLP_LEVEL_AGGR);
    return level;
}
void aec_process(const aec_handle_t *handle, int16_t *mic, int16_t *ref, int16_t *out)
{
    assert(!((uintptr_t)mic % 16) && !((uintptr_t)ref % 16) && !((uintptr_t)out % 16));
    ++aec_calls;
    for (int i = 0; i < handle->frame_size; ++i) out[i] = mic[i] - ref[i];
}

typedef struct { unsigned channels, phase; } mock_resampler_t;
esp_ae_err_t esp_ae_rate_cvt_open(esp_ae_rate_cvt_cfg_t *cfg, esp_ae_rate_cvt_handle_t *handle)
{
    assert(cfg->bits_per_sample == 16 && cfg->complexity == 2);
    assert((cfg->src_rate == 48000 && cfg->dest_rate == 16000 && cfg->channel == 2) ||
           (cfg->src_rate == 16000 && cfg->dest_rate == 48000 && cfg->channel == 1));
    if (init_failure()) return ESP_AE_ERR_MEM_LACK;
    mock_resampler_t *resampler = calloc(1, sizeof(*resampler));
    assert(resampler);
    resampler->channels = cfg->channel;
    *handle = resampler;
    ++live_resamplers;
    return ESP_AE_ERR_OK;
}
void esp_ae_rate_cvt_close(esp_ae_rate_cvt_handle_t handle)
{
    assert(handle);
    --live_resamplers;
    free(handle);
}
esp_ae_err_t esp_ae_rate_cvt_get_max_out_sample_num(esp_ae_rate_cvt_handle_t handle,
                                                  uint32_t input, uint32_t *output)
{
    if (init_failure()) return ESP_AE_ERR_FAIL;
    *output = ((mock_resampler_t *)handle)->channels == 2 ? (input + 2) / 3 : input * 3;
    return ESP_AE_ERR_OK;
}
esp_ae_err_t esp_ae_rate_cvt_reset(esp_ae_rate_cvt_handle_t handle)
{
    mock_resampler_t *resampler = handle;
    ++resampler_resets;
    if (fail_reset_channel == (int)resampler->channels) return ESP_AE_ERR_FAIL;
    resampler->phase = 0;
    return ESP_AE_ERR_OK;
}
esp_ae_err_t esp_ae_rate_cvt_process(esp_ae_rate_cvt_handle_t handle, esp_ae_sample_t input,
                                   uint32_t input_count, esp_ae_sample_t output, uint32_t *capacity)
{
    mock_resampler_t *resampler = handle;
    if (fail_process_channel == (int)resampler->channels) return ESP_AE_ERR_FAIL;
    if (oversize_channel == (int)resampler->channels) { ++*capacity; return ESP_AE_ERR_OK; }
    int16_t *in = input, *out = output;
    uint32_t produced = 0;
    for (uint32_t i = 0; i < input_count; ++i) {
        if (resampler->channels == 2) {
            if (++resampler->phase == 3) {
                resampler->phase = 0;
                assert(produced < *capacity);
                out[produced * 2] = in[i * 2];
                out[produced * 2 + 1] = in[i * 2 + 1];
                ++produced;
            }
        } else {
            assert(produced + 3 <= *capacity);
            for (int j = 0; j < 3; ++j) out[produced++] = in[i];
        }
    }
    *capacity = produced;
    return ESP_AE_ERR_OK;
}

int esp_codec_dev_open(esp_codec_dev_handle_t handle, esp_codec_dev_sample_info_t *info)
{
    assert(handle && info->sample_rate == 48000 && info->bits_per_sample == 16);
    assert(info->channel == 2 && info->channel_mask == 0);
    i2s_position = 0;
    return fail_mic_open ? ESP_CODEC_DEV_DRV_ERR : ESP_CODEC_DEV_OK;
}
int esp_codec_dev_read_reg(esp_codec_dev_handle_t handle, int reg, int *value)
{
    assert(handle && reg == 0x44);
    *value = reference_register;
    return ESP_CODEC_DEV_OK;
}
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t handle, float gain)
{
    assert(handle && gain == 30.0f);
    return ESP_CODEC_DEV_OK;
}
int esp_codec_dev_close(esp_codec_dev_handle_t handle) { assert(handle); ++mic_closes; return 0; }
int esp_codec_dev_read(esp_codec_dev_handle_t handle, void *data, int bytes)
{
    assert(handle && bytes == (int)(AEC_CAPTURE_FRAMES_48K * 2 * sizeof(int16_t)));
    ++mic_reads;
    if (fail_mic_read) return ESP_CODEC_DEV_DRV_ERR;
    int16_t *stereo = data;
    for (size_t i = 0; i < AEC_CAPTURE_FRAMES_48K; ++i, ++i2s_position) {
        stereo[i * 2] = mic_sample(i2s_position);
        stereo[i * 2 + 1] = ref_sample(i2s_position);
    }
    mock_time += 10000; /* I2S, rather than vTaskDelay(), paces the mic source. */
    return ESP_CODEC_DEV_OK;
}
bool usb_audio_is_mounted(void) { return false; }
size_t usb_audio_read(int16_t *samples, size_t count) { (void)samples; (void)count; return 0; }

static void fill_stereo(int16_t *buffer, size_t position, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        buffer[i * 2] = mic_sample(position + i);
        buffer[i * 2 + 1] = ref_sample(position + i);
    }
}
static void check_output(const int16_t *buffer, size_t position, size_t count)
{
    for (size_t i = 0; i < count; ++i) assert(buffer[i] == expected_sample(position + i));
}
static void test_pairing_and_continuity(void)
{
    assert(aec_processor_reset() == ESP_OK);
    const size_t splits[] = {1, 2, 7, 479, 480, 113, 31};
    int16_t input[480 * 2], output[2000];
    size_t consumed = 0, produced = 0;
    for (unsigned i = 0; i < 150; ++i) {
        size_t frames = splits[i % 7];
        fill_stereo(input, consumed, frames);
        assert(aec_processor_feed_capture_48k(input, frames) == ESP_OK);
        consumed += frames;
        for (;;) {
            memset(output, 0x55, sizeof(output));
            size_t count = aec_processor_read_microphone_48k(output, 137);
            check_output(output, produced, count);
            produced += count;
            /* Short reads must not modify the unused part of the caller buffer. */
            for (size_t j = count; j < 137; ++j) assert(output[j] == 0x5555);
            if (count < 137) break;
        }
    }
    assert(produced == consumed / (chunk_size * 3) * (chunk_size * 3));
    fill_stereo(input, consumed, 480);
    assert(aec_processor_feed_capture_48k(input, 480) == ESP_OK);
    assert(aec_processor_reset() == ESP_OK); /* drops queued and partial old frames */
    assert(aec_processor_read_microphone_48k(output, 2000) == 0);
    fill_stereo(input, 0, 480);
    for (int i = 0; i < 4; ++i) {
        fill_stereo(input, i * 480, 480);
        assert(aec_processor_feed_capture_48k(input, 480) == ESP_OK);
    }
    size_t count = aec_processor_read_microphone_48k(output, 2000);
    assert(count > 0);
    check_output(output, 0, count);
}
static void test_failures(void)
{
    int16_t input[480 * 2], output[2000];
    fill_stereo(input, 0, 480);
    assert(aec_processor_feed_capture_48k(NULL, 1) == ESP_ERR_INVALID_ARG);
    assert(aec_processor_feed_capture_48k(input, 481) == ESP_ERR_INVALID_ARG);
    assert(aec_processor_feed_capture_48k(input, 0) == ESP_ERR_INVALID_ARG);
    for (int channels = 1; channels <= 2; ++channels) {
        for (int oversized = 0; oversized <= 1; ++oversized) {
            assert(aec_processor_reset() == ESP_OK);
            fail_process_channel = oversized ? 0 : channels;
            oversize_channel = oversized ? channels : 0;
            esp_err_t result = ESP_OK;
            for (int i = 0; i < 5 && result == ESP_OK; ++i)
                result = aec_processor_feed_capture_48k(input, 480);
            assert(result != ESP_OK);
            assert(aec_processor_read_microphone_48k(output, 2000) == 0);
            assert(aec_processor_feed_capture_48k(input, 480) == ESP_ERR_INVALID_STATE);
            fail_process_channel = oversize_channel = 0;
        }
        fail_reset_channel = channels;
        int before = resampler_resets;
        assert(aec_processor_reset() != ESP_OK);
        assert(resampler_resets == before + 2);
        assert(aec_processor_feed_capture_48k(input, 480) == ESP_ERR_INVALID_STATE);
        fail_reset_channel = 0;
    }
    fail_aec_create = true;
    assert(aec_processor_reset() != ESP_OK);
    assert(aec_processor_feed_capture_48k(input, 480) == ESP_ERR_INVALID_STATE);
    fail_aec_create = false;
    int saved_chunk = chunk_size;
    chunk_size = saved_chunk == 160 ? 256 : 160;
    assert(aec_processor_reset() != ESP_OK);
    assert(aec_processor_reset() != ESP_OK); /* must not accept changed buffer size on retry */
    chunk_size = saved_chunk;
    assert(aec_processor_reset() == ESP_OK);
    esp_err_t result = ESP_OK;
    for (int i = 0; i < 30 && result == ESP_OK; ++i)
        result = aec_processor_feed_capture_48k(input, 480);
    assert(result == ESP_ERR_INVALID_STATE); /* overflow is explicit, never silent dropping */
    assert(aec_processor_read_microphone_48k(output, 2000) == 0);
    assert(aec_processor_reset() == ESP_OK);
}
static void test_capture_source(void)
{
    usb_capture_set_microphone((void *)(uintptr_t)1);
    esp_capture_audio_src_if_t *source = usb_capture_source(USB_CAPTURE_SOURCE_MICROPHONE);
    assert(source && source->start(source) == ESP_CAPTURE_ERR_OK);
    int16_t output[2048];
    size_t position = 0;
    const size_t requests[] = {960, 480, 137, 1920, 1, 1000, 2048};
    for (unsigned i = 0; i < 100; ++i) {
        size_t count = requests[i % 7];
        memset(output, 0x55, sizeof(output));
        esp_capture_stream_frame_t frame = {.data = (uint8_t *)output, .size = count * 2};
        assert(source->read_frame(source, &frame) == ESP_CAPTURE_ERR_OK);
        assert(frame.pts == position * 1000 / 48000);
        check_output(output, position, count);
        position += count;
    }
    assert(delays == 0 && mic_reads > 0);
    assert(i2s_position >= position && i2s_position - position < (size_t)chunk_size * 3 + 480);
    assert(source->stop(source) == ESP_CAPTURE_ERR_OK);
    esp_capture_stream_frame_t frame = {.data = (uint8_t *)output, .size = sizeof(output)};
    assert(source->read_frame(source, &frame) == ESP_CAPTURE_ERR_INVALID_STATE);
    assert(source->start(source) == ESP_CAPTURE_ERR_OK);
    assert(source->read_frame(source, &frame) == ESP_CAPTURE_ERR_OK);
    assert(frame.pts == 0);
    check_output(output, 0, 2048);
    assert(source->stop(source) == ESP_CAPTURE_ERR_OK);
    fail_mic_open = true;
    int before = mic_closes;
    assert(source->start(source) != ESP_CAPTURE_ERR_OK);
    assert(mic_closes == before + 1);
    fail_mic_open = false;
    reference_register = 0x08;
    assert(source->start(source) != ESP_CAPTURE_ERR_OK);
    assert(source->read_frame(source, &frame) == ESP_CAPTURE_ERR_INVALID_STATE);
    reference_register = 0x58;
    fail_aec_create = true;
    assert(source->start(source) != ESP_CAPTURE_ERR_OK);
    fail_aec_create = false;
    assert(source->start(source) == ESP_CAPTURE_ERR_OK);
    fail_mic_read = true;
    assert(source->read_frame(source, &frame) == ESP_CAPTURE_ERR_INTERNAL);
    fail_mic_read = false;
    assert(source->stop(source) == ESP_CAPTURE_ERR_OK);
}

int main(int argc, char **argv)
{
    if (argc > 1) chunk_size = atoi(argv[1]);
    if (argc > 2) fail_step = atoi(argv[2]);
    assert(aec_processor_reset() == ESP_ERR_INVALID_STATE);
    esp_err_t result = aec_processor_init();
    if (fail_step || chunk_size > 512) {
        assert(result != ESP_OK && live_aec == 0 && live_resamplers == 0 && live_samples == 0);
        fail_step = 0;
        chunk_size = 256;
        assert(aec_processor_init() == ESP_OK); /* partial init must be recoverable */
    } else {
        assert(result == ESP_OK);
        test_pairing_and_continuity();
        test_failures();
        test_capture_source();
#if CONFIG_MYBOT_AEC_BYPASS
        assert(aec_calls == 0);
#else
        assert(aec_calls > 0);
#endif
        printf("PASS: chunk=%d bypass=%d\n", chunk_size, CONFIG_MYBOT_AEC_BYPASS);
    }
    assert(live_aec == 1 && live_resamplers == 2 && live_samples == 5);
    return 0;
}
