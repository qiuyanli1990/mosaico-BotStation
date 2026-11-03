#include "session.h"

#include <stdatomic.h>

#include "agora_auth.h"
#include "app_state.h"
#include "bot_station.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network.h"
#include "settings.h"
#include "webrtc.h"

#define TAG "MYBOT_SESSION"
#define SESSION_TASK_STACK_SIZE (16 * 1024)
#define SESSION_LOG_INTERVAL_US (10LL * 1000 * 1000)

typedef enum {
    SESSION_WAITING_FOR_PAIRING,
    SESSION_STOPPED,
    SESSION_STARTED,
} session_request_state_t;

static atomic_int request_state;
static bot_station_conversation_t conversation;
static bool active;
static int64_t last_identity_log_us;

static void log_session_identity(const char *event)
{
    ESP_LOGI(TAG, "Session %s: agent_id=%s conversation_id=%s rtc_connected=%d wifi=%d",
             event, conversation.agent_id, conversation.conversation_id,
             webrtc_is_connected(), network_is_connected());
    last_identity_log_us = esp_timer_get_time();
}

bool session_requested(void)
{
    return atomic_load(&request_state) == SESSION_STARTED;
}

bool session_auto_start_pending(void)
{
    return atomic_load(&request_state) == SESSION_WAITING_FOR_PAIRING;
}

void session_set_languages(size_t source_index, size_t target_index)
{
    (void)source_index;
    (void)target_index;
}

void session_request(bool start)
{
    atomic_store(&request_state, start ? SESSION_STARTED : SESSION_STOPPED);
    if (!start) {
        bot_station_cancel();
        app_set_status(APP_STOPPING, "Stopping MyBot conversation");
    } else {
        app_set_status(APP_STARTING, "Starting MyBot conversation");
    }
}

static void stop_active(const char *reason)
{
    log_session_identity("stopping");
    stop_webrtc();
    if (bot_station_has_active_conversation() && network_is_connected()) {
        esp_err_t result = bot_station_stop_conversation(reason);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "MyBot stop deferred: %s", esp_err_to_name(result));
        }
    }
    active = false;
}

static bool start_active(void)
{
    if (!network_is_connected() || !network_time_ready()) {
        app_set_status(APP_ERROR, "Wi-Fi and network time are required");
        return false;
    }
    bot_station_prepare_start();
    app_set_status(APP_STARTING, "Starting MyBot conversation");
    esp_err_t result = bot_station_start_conversation(&conversation);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "MyBot conversation start failed: %s", esp_err_to_name(result));
        app_set_status(APP_ERROR, "MyBot conversation start failed; check pairing");
        return false;
    }
    char url[AGORA_DEMO_URL_SIZE];
    char token[AGORA_DEMO_TOKEN_SIZE];
    if (agora_auth_create_session(conversation.app_id,
                                  conversation.channel, conversation.uid,
                                  url, sizeof(url), token, sizeof(token)) != 0) {
        bot_station_stop_conversation("error");
        app_set_status(APP_ERROR, "Could not create WHIP credential");
        return false;
    }
    ESP_LOGI(TAG, "WHIP credential ready: channel=%s uid=%s",
             conversation.channel, conversation.uid);
    if (!session_requested()) {
        bot_station_stop_conversation("device_hangup");
        app_set_status(APP_PAIRED, "Conversation cancelled");
        return false;
    }
    app_set_status(APP_CONNECTING, "Connecting to MyBot");
    if (start_webrtc(url, token) != 0) {
        bot_station_stop_conversation("error");
        app_set_status(APP_ERROR, "WebRTC connection failed");
        return false;
    }
    active = true;
    app_set_channel(conversation.channel);
    log_session_identity("active");
    ESP_LOGI(TAG, "MyBot active channel=%s uid=%s agent_uid=%s",
             conversation.channel, conversation.uid, conversation.agent_uid);
    return true;
}

static void session_task(void *argument)
{
    (void)argument;
    for (;;) {
        app_snapshot_t state;
        app_get_snapshot(&state);
        if (state.pairing_ready && network_is_connected() && network_time_ready()) {
            /* Consume the boot-time start once. A manual stop also consumes it. */
            int expected = SESSION_WAITING_FOR_PAIRING;
            if (atomic_compare_exchange_strong(&request_state, &expected,
                                               SESSION_STARTED)) {
                ESP_LOGI(TAG, "Binding confirmed; automatically starting conversation");
            }
        }
        if (!session_requested()) {
            if (active) {
                stop_active("device_hangup");
                app_set_status(APP_IDLE, "MyBot conversation stopped");
            } else if (state.phase == APP_STOPPING) {
                app_set_status(APP_IDLE, "MyBot conversation stopped");
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (!active && !start_active()) {
            atomic_store(&request_state, SESSION_STOPPED);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        query_webrtc();
        if (esp_timer_get_time() - last_identity_log_us >= SESSION_LOG_INTERVAL_US) {
            log_session_identity("running");
        }
        if (webrtc_needs_restart()) {
            ESP_LOGW(TAG, "WebRTC needs restart; stopping MyBot conversation");
            stop_active("error");
            atomic_store(&request_state, SESSION_STOPPED);
            app_set_status(APP_ERROR, "WebRTC connection failed");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t session_init(void)
{
    atomic_store(&request_state, SESSION_WAITING_FOR_PAIRING);
    active = false;
    if (xTaskCreate(session_task, "mybot_session", SESSION_TASK_STACK_SIZE,
                    NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
