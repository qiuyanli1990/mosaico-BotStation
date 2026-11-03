#include "translation_protocol.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"

static const translation_language_t languages[TRANSLATION_LANGUAGE_COUNT] = {
    {"af-ZA", "Afrikaans", "Afrikaans", true},
    {"sq-AL", "Albanian", "Shqip", false},
    {"ar-SA", "Arabic", "العربية", true},
    {"az-AZ", "Azerbaijani", "Azərbaycan", false},
    {"eu-ES", "Basque", "Euskara", false},
    {"be-BY", "Belarusian", "Беларуская", false},
    {"bn-IN", "Bengali", "বাংলা", false},
    {"bs-BA", "Bosnian", "Bosanski", false},
    {"bg-BG", "Bulgarian", "Български", true},
    {"ca-ES", "Catalan", "Català", true},
    {"zh-CN", "Chinese", "中文", true},
    {"hr-HR", "Croatian", "Hrvatski", true},
    {"cs-CZ", "Czech", "Čeština", true},
    {"da-DK", "Danish", "Dansk", true},
    {"nl-NL", "Dutch", "Nederlands", true},
    {"en-US", "English", "English", true},
    {"et-EE", "Estonian", "Eesti", false},
    {"fi-FI", "Finnish", "Suomi", true},
    {"fr-FR", "French", "Français", true},
    {"gl-ES", "Galician", "Galego", false},
    {"de-DE", "German", "Deutsch", true},
    {"el-GR", "Greek", "Ελληνικά", true},
    {"gu-IN", "Gujarati", "ગુજરાતી", false},
    {"he-IL", "Hebrew", "עברית", true},
    {"hi-IN", "Hindi", "हिन्दी", true},
    {"hu-HU", "Hungarian", "Magyar", true},
    {"id-ID", "Indonesian", "Bahasa Indonesia", true},
    {"it-IT", "Italian", "Italiano", true},
    {"ja-JP", "Japanese", "日本語", true},
    {"kn-IN", "Kannada", "ಕನ್ನಡ", false},
    {"kk-KZ", "Kazakh", "Қазақ", false},
    {"ko-KR", "Korean", "한국어", true},
    {"lv-LV", "Latvian", "Latviešu", false},
    {"lt-LT", "Lithuanian", "Lietuvių", false},
    {"mk-MK", "Macedonian", "Македонски", false},
    {"ms-MY", "Malay", "Bahasa Melayu", true},
    {"ml-IN", "Malayalam", "മലയാളം", false},
    {"mr-IN", "Marathi", "मराठी", false},
    {"no-NO", "Norwegian", "Norsk", true},
    {"fa-IR", "Persian", "فارسی", false},
    {"pl-PL", "Polish", "Polski", true},
    {"pt-BR", "Portuguese", "Português", true},
    {"pa-IN", "Punjabi", "ਪੰਜਾਬੀ", false},
    {"ro-RO", "Romanian", "Română", true},
    {"ru-RU", "Russian", "Русский", true},
    {"sr-RS", "Serbian", "Српски", false},
    {"sk-SK", "Slovak", "Slovenčina", true},
    {"sl-SI", "Slovenian", "Slovenščina", true},
    {"es-ES", "Spanish", "Español", true},
    {"sw-KE", "Swahili", "Kiswahili", false},
    {"sv-SE", "Swedish", "Svenska", true},
    {"tl-PH", "Tagalog", "Tagalog", false},
    {"ta-IN", "Tamil", "தமிழ்", false},
    {"te-IN", "Telugu", "తెలుగు", false},
    {"th-TH", "Thai", "ไทย", true},
    {"tr-TR", "Turkish", "Türkçe", true},
    {"uk-UA", "Ukrainian", "Українська", true},
    {"ur-PK", "Urdu", "اردو", false},
    {"vi-VN", "Vietnamese", "Tiếng Việt", true},
    {"cy-GB", "Welsh", "Cymraeg", false},
};

const translation_language_t *translation_language_at(size_t index)
{
    return index < TRANSLATION_LANGUAGE_COUNT ? &languages[index] : NULL;
}

size_t translation_language_index(const char *code)
{
    if (!code) {
        return TRANSLATION_LANGUAGE_COUNT;
    }
    for (size_t i = 0; i < TRANSLATION_LANGUAGE_COUNT; ++i) {
        if (strcmp(languages[i].code, code) == 0) {
            return i;
        }
    }
    return TRANSLATION_LANGUAGE_COUNT;
}

size_t translation_tts_language_count(void)
{
    size_t count = 0;
    for (size_t i = 0; i < TRANSLATION_LANGUAGE_COUNT; ++i) {
        count += languages[i].tts_supported ? 1 : 0;
    }
    return count;
}

bool translation_language_tts_supported(const char *code)
{
    size_t index = translation_language_index(code);
    return index < TRANSLATION_LANGUAGE_COUNT && languages[index].tts_supported;
}

bool translation_channel_from_random(const unsigned char random[16], char *channel, size_t capacity)
{
    static const char hex[] = "0123456789abcdef";
    const size_t prefix = sizeof("mosaico_") - 1;
    if (!random || !channel || capacity < prefix + 32 + 1) {
        return false;
    }
    memcpy(channel, "mosaico_", prefix);
    for (size_t i = 0; i < 16; ++i) {
        channel[prefix + i * 2] = hex[random[i] >> 4];
        channel[prefix + i * 2 + 1] = hex[random[i] & 15];
    }
    channel[prefix + 32] = '\0';
    return true;
}

bool translation_valid_agent_id(const char *id)
{
    if (!id || !*id || strlen(id) >= 128) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) {
            return false;
        }
    }
    return true;
}

static bool numeric_uid(const char *uid)
{
    if (!uid || !*uid || strlen(uid) > 10) {
        return false;
    }
    unsigned long long value = 0;
    for (const unsigned char *p = (const unsigned char *)uid; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + *p - '0';
    }
    return value > 0 && value <= 0xffffffffULL;
}

char *translation_join_json(const translation_request_t *request)
{
    if (!request || !request->channel || !*request->channel ||
        strlen(request->channel) >= 64 || !request->source_language ||
        !request->target_language || translation_language_index(request->source_language) >= TRANSLATION_LANGUAGE_COUNT ||
        translation_language_index(request->target_language) >= TRANSLATION_LANGUAGE_COUNT ||
        (request->tts_enabled && (!request->voice_id || !*request->voice_id)) ||
        (request->tts_enabled && !translation_language_tts_supported(request->target_language)) ||
        !numeric_uid(request->device_uid) || !numeric_uid(request->agent_uid) ||
        strcmp(request->device_uid, request->agent_uid) == 0 || !request->idle_timeout) {
        return NULL;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON *properties = cJSON_AddObjectToObject(root, "properties");
    cJSON *remote = properties ? cJSON_AddArrayToObject(properties, "remote_rtc_uids") : NULL;
    cJSON *features = properties ? cJSON_AddObjectToObject(properties, "advanced_features") : NULL;
    cJSON *parameters = properties ? cJSON_AddObjectToObject(properties, "parameters") : NULL;
    cJSON *asr = properties ? cJSON_AddObjectToObject(properties, "asr") : NULL;
    cJSON *translation = properties ? cJSON_AddObjectToObject(properties, "translation") : NULL;
    cJSON *tts = properties ? cJSON_AddObjectToObject(properties, "tts") : NULL;
    cJSON *remote_uid = cJSON_CreateString(request->device_uid);
    if (!remote || !features || !parameters || !asr || !translation || !tts || !remote_uid) {
        cJSON_Delete(remote_uid);
        cJSON_Delete(root);
        return NULL;
    }
    if (!cJSON_AddItemToArray(remote, remote_uid)) {
        cJSON_Delete(remote_uid);
        cJSON_Delete(root);
        return NULL;
    }
    bool ok = cJSON_AddStringToObject(root, "name", request->channel) &&
        cJSON_AddStringToObject(root, "preset", "v2vt_base") &&
        cJSON_AddStringToObject(properties, "channel", request->channel) &&
        cJSON_AddStringToObject(properties, "agent_rtc_uid", request->agent_uid) &&
        cJSON_AddBoolToObject(properties, "enable_string_uid", true) &&
        cJSON_AddNumberToObject(properties, "idle_timeout", request->idle_timeout) &&
        cJSON_AddBoolToObject(features, "enable_rtm", false) &&
        cJSON_AddStringToObject(parameters, "data_channel", "datastream") &&
        cJSON_AddStringToObject(asr, "language", request->source_language) &&
        cJSON_AddStringToObject(translation, "language", request->target_language) &&
        cJSON_AddBoolToObject(tts, "enable", request->tts_enabled) &&
        (!request->tts_enabled || cJSON_AddStringToObject(tts, "voice_id", request->voice_id));
    /* Certificate-disabled project: deliberately omit the RTC token field. */
    char *serialized = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    return serialized;
}

static bool parse_string(const char *json, const char *key, char *out, size_t capacity)
{
    if (!json || !out || !capacity) {
        return false;
    }
    out[0] = '\0';
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithOpts(json, &end, true);
    cJSON *field = root ? cJSON_GetObjectItemCaseSensitive(root, key) : NULL;
    bool ok = cJSON_IsObject(root) && cJSON_IsString(field) && *field->valuestring &&
              strlen(field->valuestring) < capacity;
    if (ok) {
        memcpy(out, field->valuestring, strlen(field->valuestring) + 1);
    }
    cJSON_Delete(root);
    return ok;
}

bool translation_parse_agent(const char *json, char *agent_id, size_t capacity)
{
    if (!parse_string(json, "agent_id", agent_id, capacity)) {
        return false;
    }
    if (!translation_valid_agent_id(agent_id)) {
        agent_id[0] = '\0';
        return false;
    }
    return true;
}

bool translation_parse_status(const char *json, char *status, size_t capacity)
{
    return parse_string(json, "status", status, capacity);
}

