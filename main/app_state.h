#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    APP_IDLE,
    APP_WAITING_FOR_WIFI,
    APP_SYNCING_TIME,
    APP_VERIFYING_BINDING,
    APP_REQUESTING_PAIR_CODE,
    APP_WAITING_FOR_BINDING,
    APP_PAIRED,
    APP_STARTING,
    APP_CONNECTING,
    APP_RUNNING,
    APP_STOPPING,
    APP_ERROR,
} app_phase_t;

typedef struct {
    app_phase_t phase;
    char message[128];
    char channel[64];
    char pair_code[7];
    bool pairing_ready;
    char agent_state[16];
    char user_transcript[112];
    char assistant_transcript[112];
} app_snapshot_t;

void app_set_status(app_phase_t phase, const char *message);
void app_set_channel(const char *channel);
void app_set_pair_code(const char *pair_code);
void app_set_pairing_ready(bool ready);
void app_set_agent_state(const char *agent_state);
void app_set_transcript(bool assistant, const char *text);
void app_get_snapshot(app_snapshot_t *snapshot);

