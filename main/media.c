#include "media.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "av_render.h"
#include "av_render_default.h"
#include "aec_processor.h"
#include "audio_render.h"
#include "bsp/esp_mosaico.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_enc_default.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "session.h"
#include "network.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "usb_audio.h"
#include "usb_capture.h"

static esp_codec_dev_handle_t speaker;
static esp_codec_dev_handle_t microphone;
static esp_capture_handle_t captures[MEDIA_SOURCE_COUNT];
static av_render_handle_t players[MEDIA_SOURCE_COUNT];
static audio_render_handle_t gated_renderers[MEDIA_SOURCE_COUNT];
static atomic_bool enabled[MEDIA_SOURCE_COUNT];
static atomic_uint volume = MEDIA_DEFAULT_SPEAKER_VOLUME;
static atomic_uint rendered_frames[MEDIA_SOURCE_COUNT];
static portMUX_TYPE output_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t last_output_us[MEDIA_SOURCE_COUNT];

typedef struct {
    audio_render_handle_t inner;
    media_source_t source;
    unsigned applied_volume;
} gated_render_t;

/* Opus and DTLS need larger stacks than the library's generic defaults. */
static void media_thread_schedule(const char *name, media_lib_thread_cfg_t *config)
{
    if (strcmp(name, "aenc_0") == 0) {
        config->stack_size = 40 * 1024;
        config->priority = 10;
        config->core_id = 1;
    } else if (strcmp(name, "AUD_SRC") == 0 || strcmp(name, "Adec") == 0) {
        config->stack_size = 40 * 1024;
        config->priority = 15;
    } else if (strcmp(name, "pc_task") == 0) {
        config->stack_size = 25 * 1024;
        config->priority = 18;
        config->core_id = 1;
    }
}

static void capture_thread_schedule(const char *name, esp_capture_thread_schedule_cfg_t *config)
{
    media_lib_thread_cfg_t schedule = {
        .stack_size = config->stack_size, .priority = config->priority,
        .core_id = config->core_id,
    };
    media_thread_schedule(name, &schedule);
    config->stack_size = schedule.stack_size;
    config->priority = schedule.priority;
    config->core_id = schedule.core_id;
    config->stack_in_ext = true;
}

static audio_render_handle_t gate_init(void *config, int size)
{
    if (size != sizeof(gated_render_t)) {
        return NULL;
    }
    gated_render_t *gate = calloc(1, sizeof(*gate));
    if (gate) {
        *gate = *(gated_render_t *)config;
    }
    return gate;
}

static int gate_open(audio_render_handle_t handle, av_render_audio_frame_info_t *info)
{
    gated_render_t *gate = handle;
    int result = audio_render_open(gate->inner, info);
    if (!result) {
        gate->applied_volume = atomic_load(&volume);
        result = esp_codec_dev_set_out_vol(speaker, gate->applied_volume);
        if (result != ESP_CODEC_DEV_OK) {
            audio_render_close(gate->inner);
        }
    }
    return result;
}

static int gate_write(audio_render_handle_t handle, av_render_audio_frame_t *frame)
{
    gated_render_t *gate = handle;
    if (!atomic_load(&enabled[gate->source]) || !session_requested() || !network_is_connected()) {
        return 0;
    }
    unsigned requested_volume = atomic_load(&volume);
    if (requested_volume != gate->applied_volume) {
        if (esp_codec_dev_set_out_vol(speaker, requested_volume) != ESP_CODEC_DEV_OK) {
            return -1;
        }
        gate->applied_volume = requested_volume;
    }
    /* ES8311 returns the DAC reference in the capture's right I2S slot.
     * No asynchronous playback queue is used as an AEC reference. */
    int result = audio_render_write(gate->inner, frame);
    if (!result && frame->size > 0) {
        portENTER_CRITICAL(&output_lock);
        last_output_us[gate->source] = esp_timer_get_time();
        portEXIT_CRITICAL(&output_lock);
        atomic_fetch_add(&rendered_frames[gate->source], 1);
    }
    return result;
}

static int gate_latency(audio_render_handle_t handle, uint32_t *latency)
{
    return audio_render_get_latency(((gated_render_t *)handle)->inner, latency);
}

static int gate_info(audio_render_handle_t handle, av_render_audio_frame_info_t *info)
{
    return audio_render_get_frame_info(((gated_render_t *)handle)->inner, info);
}

static int gate_speed(audio_render_handle_t handle, float speed)
{
    return audio_render_set_speed(((gated_render_t *)handle)->inner, speed);
}

static int gate_close(audio_render_handle_t handle)
{
    return audio_render_close(((gated_render_t *)handle)->inner);
}

static void gate_free(audio_render_handle_t handle)
{
    gated_render_t *gate = handle;
    audio_render_free_handle(gate->inner);
    free(gate);
}

esp_err_t media_init(void)
{
    esp_err_t result = media_lib_add_default_adapter();
    if (result != ESP_OK) {
        return result;
    }
    media_lib_thread_set_schedule_cb(media_thread_schedule);
    if (esp_capture_set_thread_scheduler(capture_thread_schedule) != ESP_CAPTURE_ERR_OK) {
        return ESP_FAIL;
    }
    if (esp_audio_enc_register_default() != ESP_AUDIO_ERR_OK ||
        esp_audio_dec_register_default() != ESP_AUDIO_ERR_OK) {
        return ESP_FAIL;
    }
    speaker = bsp_audio_codec_speaker_init();
    microphone = bsp_audio_codec_microphone_init();
    if (!speaker || !microphone) {
        return ESP_FAIL;
    }
    if (aec_processor_init() != ESP_OK) {
        return ESP_FAIL;
    }
    usb_capture_set_microphone(microphone);
    return ESP_OK;
}

esp_err_t media_session_open(media_source_t source, esp_webrtc_media_provider_t *provider)
{
    if (source < 0 || source >= MEDIA_SOURCE_COUNT || !provider ||
        !speaker || !microphone || captures[source] || players[source]) {
        return ESP_ERR_INVALID_STATE;
    }
    media_set_enabled(source, false);
    atomic_store(&rendered_frames[source], 0);
    esp_capture_cfg_t capture_config = {
        .sync_mode = ESP_CAPTURE_SYNC_MODE_NONE,
        .audio_src = usb_capture_source(source == MEDIA_SOURCE_USB_SPEAKER ?
                                        USB_CAPTURE_SOURCE_SPEAKER :
                                        USB_CAPTURE_SOURCE_MICROPHONE),
    };
    if (esp_capture_open(&capture_config, &captures[source]) != ESP_CAPTURE_ERR_OK) {
        captures[source] = NULL;
        return ESP_FAIL;
    }
    i2s_render_cfg_t output_config = {.fixed_clock = true, .play_handle = speaker};
    audio_render_handle_t inner = av_render_alloc_i2s_render(&output_config);
    if (!inner) {
        media_session_close(source);
        return ESP_ERR_NO_MEM;
    }
    gated_render_t gate_context = {.inner = inner, .source = source};
    audio_render_cfg_t gate_config = {
        .ops = {.init = gate_init, .open = gate_open, .write = gate_write,
                .get_latency = gate_latency, .get_frame_info = gate_info,
                .set_speed = gate_speed, .close = gate_close, .deinit = gate_free},
        .cfg = &gate_context, .cfg_size = sizeof(gate_context),
    };
    gated_renderers[source] = audio_render_alloc_handle(&gate_config);
    if (!gated_renderers[source]) {
        audio_render_free_handle(inner);
        media_session_close(source);
        return ESP_ERR_NO_MEM;
    }
    av_render_cfg_t config = {
        .audio_render = gated_renderers[source],
        .audio_raw_fifo_size = 4 * 1024,
        .audio_render_fifo_size = 16 * 1024,
        .allow_drop_data = false,
    };
    players[source] = av_render_open(&config);
    uint32_t threshold = 4800 * sizeof(int16_t);
    if (!players[source] || av_render_set_audio_threshold(players[source], threshold) != 0) {
        media_session_close(source);
        return ESP_FAIL;
    }
    *provider = (esp_webrtc_media_provider_t) {
        .capture = captures[source], .player = players[source]
    };
    ESP_LOGI("MEDIA", "ES8311 mic + DAC hardware reference -> AEC -> "
             "48 kHz mono Opus; downlink=ES8311 speaker");
    return ESP_OK;
}

void media_session_close(media_source_t source)
{
    if (source < 0 || source >= MEDIA_SOURCE_COUNT) {
        return;
    }
    media_set_enabled(source, false);
    if (captures[source]) {
        esp_capture_close(captures[source]);
        captures[source] = NULL;
    }
    if (players[source]) {
        av_render_close(players[source]);
        players[source] = NULL;
    }
    if (gated_renderers[source]) {
        audio_render_free_handle(gated_renderers[source]);
        gated_renderers[source] = NULL;
    }
}

void media_set_enabled(media_source_t source, bool allow)
{
    if (source < 0 || source >= MEDIA_SOURCE_COUNT) {
        return;
    }
    atomic_store(&enabled[source], allow);
    usb_audio_set_forwarding(atomic_load(&enabled[MEDIA_SOURCE_USB_SPEAKER]),
                             atomic_load(&enabled[MEDIA_SOURCE_ONBOARD_MIC]));
}

void media_set_volume(unsigned value)
{
    atomic_store(&volume, value > 100 ? 100 : value);
}

unsigned media_get_volume(void)
{
    return atomic_load(&volume);
}

uint32_t media_rendered_frames(void)
{
    return atomic_load(&rendered_frames[MEDIA_SOURCE_USB_SPEAKER]) +
           atomic_load(&rendered_frames[MEDIA_SOURCE_ONBOARD_MIC]);
}

bool media_is_speaking(void)
{
    portENTER_CRITICAL(&output_lock);
    int64_t last = last_output_us[MEDIA_SOURCE_ONBOARD_MIC];
    portEXIT_CRITICAL(&output_lock);
    return atomic_load(&enabled[MEDIA_SOURCE_ONBOARD_MIC]) && session_requested() &&
           last && esp_timer_get_time() - last < 300000;
}

