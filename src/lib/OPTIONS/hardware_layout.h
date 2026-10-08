#pragma once

// Layout-document helpers shared by the ESP and STM32 hardware loaders (hardware.cpp).

#include <ArduinoJson.h>

// Sets every field to its "absent" value (pins -1, flags false, arrays empty).
void hardware_ClearAllFields();
// Loads every field present in `doc`. A string value for a pin field is an STM32 pin name ("PE12").
void hardware_LoadFieldsFromDoc(JsonDocument &doc);
// The layout an override produces: `overrideDoc` as-is, the `config_flash_*` keys taken from
// `slotDoc` (or removed when the slot has none), and `"customised": true`.
JsonDocument hardware_ApplyOverride(JsonDocument &slotDoc, JsonDocument &overrideDoc);

#if defined(PLATFORM_STM32)
// The layout flashed in the firmware slot, as parsed by hardware_init().
JsonDocument &hardware_SlotDoc();
// Replaces the loaded fields and getHardware() with `doc`.
void hardware_LoadDoc(JsonDocument &doc);
#endif
