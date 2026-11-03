#include "subtitles.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "mbedtls/base64.h"

#define MAX_STREAM_SPLIT 4U
#define MAX_STREAM_PAYLOAD_LEN (8U * 1024U)

static portMUX_TYPE subtitle_lock = portMUX_INITIALIZER_UNLOCKED;
static char subtitle_text[SUBTITLE_TEXT_CAPACITY];
static uint32_t subtitle_revision;
static char *split_payload;
static char split_message_id[64];
static unsigned split_expected_total;

static bool contains_ci(const char *text, const char *needle)
{
    if (!text || !needle || !*needle) {
        return false;
    }
    size_t needle_length = strlen(needle);
    for (const char *cursor = text; *cursor; ++cursor) {
        if (strncasecmp(cursor, needle, needle_length) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_subtitle_event(cJSON *root, bool *has_event)
{
    static const char *event_keys[] = {"object", "event", "event_type", "type", "name"};
    *has_event = false;
    for (size_t i = 0; i < sizeof(event_keys) / sizeof(event_keys[0]); ++i) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(root, event_keys[i]);
        if (!cJSON_IsString(item) || !item->valuestring) {
            continue;
        }
        *has_event = true;
        if (contains_ci(item->valuestring, "user") || contains_ci(item->valuestring, "state")) {
            return false;
        }
        if (contains_ci(item->valuestring, "agent") ||
            contains_ci(item->valuestring, "translation") ||
            contains_ci(item->valuestring, "transcript")) {
            return true;
        }
    }
    return false;
}

static const char *find_text(cJSON *node, unsigned depth)
{
    static const char *text_keys[] = {
        "translated_text", "translation", "text", "transcript", "content", "message"
    };
    if (!node || depth > 5) {
        return NULL;
    }
    if (cJSON_IsString(node) && node->valuestring && *node->valuestring) {
        return node->valuestring;
    }
    if (!cJSON_IsObject(node)) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(text_keys) / sizeof(text_keys[0]); ++i) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(node, text_keys[i]);
        const char *text = find_text(item, depth + 1);
        if (text && *text) {
            return text;
        }
    }
    for (cJSON *item = node->child; item; item = item->next) {
        const char *text = find_text(item, depth + 1);
        if (text && *text) {
            return text;
        }
    }
    return NULL;
}

static void set_text(const char *text)
{
    if (!text || !*text) {
        return;
    }
    size_t length = strlen(text);
    if (length >= sizeof(subtitle_text)) {
        length = sizeof(subtitle_text) - 1;
    }
    portENTER_CRITICAL(&subtitle_lock);
    if (strncmp(subtitle_text, text, length) != 0 || subtitle_text[length] != '\0') {
        memcpy(subtitle_text, text, length);
        subtitle_text[length] = '\0';
        ++subtitle_revision;
    }
    portEXIT_CRITICAL(&subtitle_lock);
}

static void reset_split(void)
{
    free(split_payload);
    split_payload = NULL;
    split_message_id[0] = '\0';
    split_expected_total = 0;
}

static bool handle_json_or_text(const char *text)
{
    cJSON *root = cJSON_Parse(text);
    if (root) {
        bool has_event = false;
        bool subtitle_event = is_subtitle_event(root, &has_event);
        const char *subtitle = find_text(root, 0);
        if (subtitle && (!has_event || subtitle_event)) {
            set_text(subtitle);
        }
        cJSON_Delete(root);
        return true;
    }
    bool printable = true;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        if (!isprint(*cursor) && !isspace(*cursor)) {
            printable = false;
            break;
        }
    }
    if (printable) {
        set_text(text);
    }
    return printable;
}

static bool handle_split(char *message)
{
    char *save = NULL;
    char *message_id = strtok_r(message, "|", &save);
    char *index_text = strtok_r(NULL, "|", &save);
    char *total_text = strtok_r(NULL, "|", &save);
    char *payload = strtok_r(NULL, "|", &save);
    if (!message_id || !index_text || !total_text || !payload) {
        return false;
    }
    unsigned index = (unsigned)strtoul(index_text, NULL, 10);
    unsigned total = (unsigned)strtoul(total_text, NULL, 10);
    if (!index || !total || index > total || total > MAX_STREAM_SPLIT) {
        reset_split();
        return false;
    }
    if (!split_payload || strcmp(split_message_id, message_id) != 0 ||
        split_expected_total != total || index == 1) {
        reset_split();
        snprintf(split_message_id, sizeof(split_message_id), "%s", message_id);
        split_expected_total = total;
        split_payload = calloc(MAX_STREAM_PAYLOAD_LEN + 1, 1);
    }
    if (!split_payload || strlen(split_payload) + strlen(payload) >= MAX_STREAM_PAYLOAD_LEN) {
        reset_split();
        return true;
    }
    strcat(split_payload, payload);
    if (index == total) {
        size_t decoded_length = 0;
        unsigned char *decoded = calloc(strlen(split_payload) + 1, 1);
        if (decoded && mbedtls_base64_decode(decoded, strlen(split_payload) + 1,
                                             &decoded_length,
                                             (const unsigned char *)split_payload,
                                             strlen(split_payload)) == 0) {
            decoded[decoded_length] = '\0';
            handle_json_or_text((const char *)decoded);
        }
        free(decoded);
        reset_split();
    }
    return true;
}

void subtitles_clear(void)
{
    reset_split();
    portENTER_CRITICAL(&subtitle_lock);
    subtitle_text[0] = '\0';
    ++subtitle_revision;
    portEXIT_CRITICAL(&subtitle_lock);
}

void subtitles_handle_data(const uint8_t *data, size_t length)
{
    if (!data || !length || length > MAX_STREAM_PAYLOAD_LEN) {
        return;
    }
    char *message = calloc(length + 1, 1);
    if (!message) {
        return;
    }
    memcpy(message, data, length);
    char *trimmed = message;
    while (*trimmed && isspace((unsigned char)*trimmed)) {
        ++trimmed;
    }
    size_t trimmed_length = strlen(trimmed);
    char *split_copy = calloc(trimmed_length + 1, 1);
    if (split_copy) {
        memcpy(split_copy, trimmed, trimmed_length);
    }
    if (split_copy && handle_split(split_copy)) {
        free(split_copy);
        free(message);
        return;
    }
    free(split_copy);
    handle_json_or_text(trimmed);
    free(message);
}

uint32_t subtitles_generation(void)
{
    uint32_t revision;
    portENTER_CRITICAL(&subtitle_lock);
    revision = subtitle_revision;
    portEXIT_CRITICAL(&subtitle_lock);
    return revision;
}

bool subtitles_copy(char *out, size_t capacity)
{
    if (!out || !capacity) {
        return false;
    }
    portENTER_CRITICAL(&subtitle_lock);
    snprintf(out, capacity, "%s", subtitle_text);
    bool present = subtitle_text[0] != '\0';
    portEXIT_CRITICAL(&subtitle_lock);
    return present;
}
