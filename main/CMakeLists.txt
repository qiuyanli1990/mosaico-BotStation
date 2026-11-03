idf_component_register(
    SRCS "app_main.c" "app_state.c" "network.c" "session.c" "bot_station.c"
         "aec_processor.c" "device_power.c"
         "media.c" "usb_audio.c" "pcm_ring.c"
         "usb_console.c"
         "usb_capture.c" "ui.c" "pair_code_font.c" "agora_auth.c" "agora_signaling.c"
         "stream_messages.c" "webrtc.c"
    INCLUDE_DIRS "."
    REQUIRES esp-mosaico-bsp esp_webrtc esp_peer av_render media_lib_utils
             esp_capture esp_audio_codec esp_codec_dev usb_device_uac
             esp_event esp_wifi esp_netif esp_http_client esp_timer esp_app_format
             nvs_flash espressif__cjson espressif__esp-sr espressif__esp_audio_effects
             mbedtls lwip console
             espressif__tinyusb)

# Preserve the UAC component's internal stream reset before our lifecycle hooks.
idf_component_get_property(uac_lib espressif__usb_device_uac COMPONENT_LIB)
target_compile_definitions(${uac_lib} PRIVATE
    tud_mount_cb=uac_device_mount_cb
    tud_umount_cb=uac_device_umount_cb
    tud_suspend_cb=uac_device_suspend_cb
    tud_resume_cb=uac_device_resume_cb)

idf_component_get_property(tusb_lib espressif__tinyusb COMPONENT_LIB)
idf_component_get_property(uac_component_dir espressif__usb_device_uac COMPONENT_DIR)
target_include_directories(${tusb_lib} PUBLIC
    "${uac_component_dir}/tusb"
    "${uac_component_dir}/tusb_uac")
set(usb_speed_definitions)
if(CONFIG_MOSAICO_USB_HIGH_SPEED)
    list(APPEND usb_speed_definitions CONFIG_TINYUSB_RHPORT_HS=1)
endif()
target_compile_definitions(${tusb_lib} PUBLIC
    CFG_TUD_CDC=1
    CFG_TUD_CDC_RX_BUFSIZE=256
    CFG_TUD_CDC_TX_BUFSIZE=1024
    CFG_TUD_CDC_EP_BUFSIZE=512
    ${usb_speed_definitions})
target_compile_definitions(${uac_lib} PRIVATE ${usb_speed_definitions})
target_sources(${tusb_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/usb_descriptors.c")

