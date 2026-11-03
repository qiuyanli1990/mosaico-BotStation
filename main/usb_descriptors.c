#include <string.h>

#include "tusb.h"
#include "uac_descriptors.h"
#include "usb_descriptors.h"

#define DEVICE_VID 0x303A
#define DEVICE_PID 0x4012
#define CDC_NOTIFY_EP 0x83
#define CDC_OUT_EP 0x04
#define CDC_IN_EP 0x84
#define AUDIO_OUT_EP 0x01
#define AUDIO_FB_EP 0x81
#define AUDIO_IN_EP 0x82

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_AUDIO_DEVICE_DESC_LEN + TUD_CDC_DESC_LEN)

static tusb_desc_device_t const device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = DEVICE_VID,
    .idProduct = DEVICE_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static tusb_desc_device_qualifier_t const device_qualifier = {
    .bLength = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 1,
};

static uint8_t const high_speed_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 7, CDC_NOTIFY_EP, 8, CDC_OUT_EP, CDC_IN_EP, 512),
    TUD_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, AUDIO_OUT_EP, AUDIO_IN_EP, AUDIO_FB_EP),
};

static uint8_t const full_speed_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x80, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 7, CDC_NOTIFY_EP, 8, CDC_OUT_EP, CDC_IN_EP, 64),
    TUD_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, 4, AUDIO_OUT_EP, AUDIO_IN_EP, AUDIO_FB_EP),
};

static uint8_t other_speed_configuration[CONFIG_TOTAL_LEN];

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&device_descriptor;
}

uint8_t const *tud_descriptor_device_qualifier_cb(void)
{
    return (uint8_t const *)&device_qualifier;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return tud_speed_get() == TUSB_SPEED_HIGH ? high_speed_configuration : full_speed_configuration;
}

uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void)index;
    const uint8_t *configuration = tud_speed_get() == TUSB_SPEED_HIGH
        ? full_speed_configuration : high_speed_configuration;
    memcpy(other_speed_configuration, configuration, sizeof(other_speed_configuration));
    other_speed_configuration[1] = TUSB_DESC_OTHER_SPEED_CONFIG;
    return other_speed_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static char const *const strings[] = {
        "",
        "Mosaico",
        "Mosaico Translator",
        "12345678",
        "Audio Control",
        "Speaker",
        "Microphone",
        "Serial Console",
    };
    static uint16_t descriptor[32];

    if (index >= sizeof(strings) / sizeof(strings[0])) {
        return NULL;
    }
    if (index == 0) {
        descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
        descriptor[1] = 0x0409;
        return descriptor;
    }
    size_t length = strlen(strings[index]);
    if (length > 31) {
        length = 31;
    }
    for (size_t i = 0; i < length; ++i) {
        descriptor[i + 1] = (uint8_t)strings[index][i];
    }
    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * length + 2));
    return descriptor;
}
