#ifndef LV_CONF_H
#define LV_CONF_H

/* Match the device's RGB565 renderer and fixed 64 KiB LVGL pool. */
#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (64 * 1024U)
#define LV_MEM_POOL_EXPAND_SIZE 0
#define LV_USE_OS LV_OS_NONE
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_SOURCE_HAN_SANS_SC_16_CJK 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER abort();

#endif
