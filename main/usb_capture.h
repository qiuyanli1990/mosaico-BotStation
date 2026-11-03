#pragma once
#include "esp_capture_audio_src_if.h"
#include "esp_codec_dev.h"

typedef enum {
    USB_CAPTURE_SOURCE_SPEAKER,
    USB_CAPTURE_SOURCE_MICROPHONE,
} usb_capture_source_id_t;

void usb_capture_set_microphone(esp_codec_dev_handle_t microphone);
esp_capture_audio_src_if_t *usb_capture_source(usb_capture_source_id_t source);

