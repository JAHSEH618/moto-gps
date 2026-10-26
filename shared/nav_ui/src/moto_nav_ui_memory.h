#pragma once

// Placement for large, zero-initialised UI state.
//
// On the board internal RAM is the scarce pool: the BLE stack, the LVGL task
// and every FreeRTOS stack come out of it. Objects tagged MOTO_UI_EXT_RAM go to
// PSRAM's .bss instead (CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY). They must
// be zero-initialised: a non-zero default member initialiser moves the object
// to .data, which cannot live in PSRAM. Elsewhere the tag is empty.
#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#define MOTO_UI_EXT_RAM EXT_RAM_BSS_ATTR
#else
#define MOTO_UI_EXT_RAM
#endif
