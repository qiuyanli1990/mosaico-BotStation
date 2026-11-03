#include "stream_messages.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "app_state.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define TAG "STREAM_MSG"
#define STREAM_MESSAGE_MAX_SIZE (8U * 1024U)
#define STREAM_QUEUE_DEPTH 4U
#define STREAM_TASK_STACK_SIZE 6144U

typedef struct {
    size_t length;
    uint8_t data[];
} queued_message_t;

static QueueHandle_t message_queue;
static char nested_values[6][160];

static const char *object_string(cJSON *root)
{
    cJSON *object = cJSON_GetObjectItemCaseSensitive(root, "object");
    return cJSON_IsString(object) ? object->valuestring : NULL;
}

static const char *find_string(cJSON *node, const char *key, unsigned depth)
{
    if (!node || depth > 5) {
        return NULL;
    }
    if (cJSON_IsString(node) && node->valuestring[0] == '{') {
        cJSON *nested = cJSON_Parse(node->valuestring);
        const char *value = find_string(nested, key, depth + 1);
        if (value) {
            char *nested_value = nested_values[depth];
            snprintf(nested_value, sizeof(nested_values[depth]), "%s", value);
            cJSON_Delete(nested);
            return nested_value;
        }
        cJSON_Delete(nested);
        return NULL;
    }
    if (!cJSON_IsObject(node) && !cJSON_IsArray(node)) {
        return NULL;
    }
    cJSON *item = cJSON_GetObjectItemCaseSensitive(node, key);
    if (cJSON_IsString(item) && item->valuestring[0]) {
        return item->valuestring;
    }
    for (item = node->child; item; item = item->next) {
        const char *value = find_string(item, key, depth + 1);
        if (value) {
            return value;
        }
    }
    return NULL;
}

static const char *find_transcript(cJSON *root)
{
    static const char *keys[] = {"text", "transcript", "content"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const char *value = find_string(root, keys[i], 0);
        if (value && value[0]) {
            return value;
        }
    }
    return NULL;
}

static void parse_user_action(cJSON *root)
{
    cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "content");
    if (!cJSON_IsString(content) || content->valuestring[0] != '{') {
        return;
    }
    cJSON *payload = cJSON_Parse(content->valuestring);
    if (!payload) {
        return;
    }
    const char *action = find_string(payload, "action_type", 0);
    if (!action) {
        action = find_string(payload, "action", 0);
    }
    if (action) {
        ESP_LOGI(TAG, "Device action parsed: %s", action);
    }
    cJSON_Delete(payload);
}

static void parse_message(const uint8_t *data, size_t length)
{
    cJSON *root = cJSON_ParseWithLength((const char *)data, length);
    if (!root) {
        ESP_LOGW(TAG, "Ignoring non-JSON data-channel message (%u bytes)",
                 (unsigned)length);
        return;
    }
    cJSON *nested = NULL;
    cJSON *source = root;
    if (!object_string(source)) {
        static const char *wrapper_keys[] = {"message", "data", "content"};
        for (size_t i = 0; i < sizeof(wrapper_keys) / sizeof(wrapper_keys[0]); ++i) {
            cJSON *wrapper = cJSON_GetObjectItemCaseSensitive(root,
                                                              wrapper_keys[i]);
            if (cJSON_IsString(wrapper) && wrapper->valuestring[0] == '{') {
                nested = cJSON_Parse(wrapper->valuestring);
                if (object_string(nested)) {
                    source = nested;
                    break;
                }
                cJSON_Delete(nested);
                nested = NULL;
            }
        }
    }
    const char *object = object_string(source);
    if (!object) {
        ESP_LOGD(TAG, "Ignoring datastream object without type (%u bytes)",
                 (unsigned)length);
    } else if (strcmp(object, "message.state") == 0) {
        const char *state = find_string(source, "state", 0);
        if (state && (strcmp(state, "silent") == 0 ||
                      strcmp(state, "listening") == 0 ||
                      strcmp(state, "thinking") == 0 ||
                      strcmp(state, "speaking") == 0)) {
            app_set_agent_state(state);
            ESP_LOGI(TAG, "Agent state: %s", state);
        } else {
            ESP_LOGW(TAG, "Ignoring unrecognized message.state payload");
        }
    } else if (strcmp(object, "user.transcription") == 0 ||
               strcmp(object, "assistant.transcription") == 0) {
        const char *text = find_transcript(source);
        if (text) {
            bool assistant = strcmp(object, "assistant.transcription") == 0;
            app_set_transcript(assistant, text);
            ESP_LOGI(TAG, "Transcript received: role=%s bytes=%u",
                     assistant ? "assistant" : "user",
                     (unsigned)strlen(text));
        }
    } else if (strcmp(object, "message.user") == 0) {
        parse_user_action(source);
    } else {
        ESP_LOGD(TAG, "Ignoring datastream object=%s", object);
    }
    cJSON_Delete(nested);
    cJSON_Delete(root);
}

static void stream_message_task(void *argument)
{
    (void)argument;
    queued_message_t *message;
    for (;;) {
        if (xQueueReceive(message_queue, &message, portMAX_DELAY) == pdTRUE) {
            parse_message(message->data, message->length);
            heap_caps_free(message);
        }
    }
}

esp_err_t stream_messages_init(void)
{
    if (message_queue) {
        return ESP_OK;
    }
    message_queue = xQueueCreate(STREAM_QUEUE_DEPTH,
                                 sizeof(queued_message_t *));
    if (!message_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(stream_message_task, "stream_message",
                    STREAM_TASK_STACK_SIZE, NULL, 4, NULL) != pdPASS) {
        vQueueDelete(message_queue);
        message_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void stream_messages_clear(void)
{
    if (message_queue) {
        queued_message_t *message;
        while (xQueueReceive(message_queue, &message, 0) == pdTRUE) {
            heap_caps_free(message);
        }
    }
    app_set_agent_state("");
    app_set_transcript(false, "");
    app_set_transcript(true, "");
}

int stream_messages_on_custom_data(esp_webrtc_custom_data_via_t via,
                                   uint8_t *data, int size, void *ctx)
{
    (void)ctx;
    if (via != ESP_WEBRTC_CUSTOM_DATA_VIA_DATA_CHANNEL || !message_queue ||
        !data || size <= 0 || (unsigned)size > STREAM_MESSAGE_MAX_SIZE) {
        return 0;
    }
    queued_message_t *message = heap_caps_malloc(
        sizeof(*message) + (size_t)size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!message) {
        ESP_LOGW(TAG, "Dropping datastream message: PSRAM allocation failed");
        return 0;
    }
    message->length = (size_t)size;
    memcpy(message->data, data, message->length);
    if (xQueueSend(message_queue, &message, 0) != pdTRUE) {
        heap_caps_free(message);
        ESP_LOGW(TAG, "Dropping datastream message: parser queue is full");
    }
    return 0;
}
