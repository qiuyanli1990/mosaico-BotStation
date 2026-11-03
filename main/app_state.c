#include "app_state.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static app_snapshot_t state = {
    .phase = APP_WAITING_FOR_WIFI,
    .message = "Waiting for Wi-Fi",
};

void app_set_status(app_phase_t phase, const char *message)
{
    portENTER_CRITICAL(&state_lock);
    state.phase = phase;
    snprintf(state.message, sizeof(state.message), "%s", message);
    portEXIT_CRITICAL(&state_lock);
}

void app_set_channel(const char *channel)
{
    portENTER_CRITICAL(&state_lock);
    snprintf(state.channel, sizeof(state.channel), "%s", channel);
    portEXIT_CRITICAL(&state_lock);
}

void app_set_pair_code(const char *pair_code)
{
    portENTER_CRITICAL(&state_lock);
    snprintf(state.pair_code, sizeof(state.pair_code), "%s",
             pair_code ? pair_code : "");
    portEXIT_CRITICAL(&state_lock);
}

void app_set_pairing_ready(bool ready)
{
    portENTER_CRITICAL(&state_lock);
    state.pairing_ready = ready;
    portEXIT_CRITICAL(&state_lock);
}

void app_set_agent_state(const char *agent_state)
{
    portENTER_CRITICAL(&state_lock);
    snprintf(state.agent_state, sizeof(state.agent_state), "%s",
             agent_state ? agent_state : "");
    portEXIT_CRITICAL(&state_lock);
}

void app_set_transcript(bool assistant, const char *text)
{
    portENTER_CRITICAL(&state_lock);
    char *destination = assistant ? state.assistant_transcript :
                                    state.user_transcript;
    size_t capacity = assistant ? sizeof(state.assistant_transcript) :
                                  sizeof(state.user_transcript);
    snprintf(destination, capacity, "%s", text ? text : "");
    portEXIT_CRITICAL(&state_lock);
}

void app_get_snapshot(app_snapshot_t *snapshot)
{
    portENTER_CRITICAL(&state_lock);
    *snapshot = state;
    portEXIT_CRITICAL(&state_lock);
}

