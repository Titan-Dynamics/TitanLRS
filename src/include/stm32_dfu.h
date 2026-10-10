#pragma once

#include "targets.h"

// Reboot into the MCU's ROM DFU bootloader. The request sets an RTC backup-register flag and
// resets; on the next boot stm32_DfuCheckAndJump() (called from initVariant(), before setup())
// sees the flag and jumps to the ROM bootloader, which enumerates as the standard USB DFU device.
// No custom bootloader and no flash-layout change: the ROM DFU cannot be corrupted, so a bad
// application image is always recoverable.
//
// STM32_DFU_SUPPORTED is defined only for the MCU families stm32_dfu.cpp has constants for.
// Everything else keys off it, never off a family macro.

#if defined(PLATFORM_STM32) && defined(STM32H7xx)
#define STM32_DFU_SUPPORTED
#endif

#if defined(STM32_DFU_SUPPORTED)
/// Set the DFU flag and reset. Does not return.
void stm32_RequestDfu();

/// Jump to the ROM bootloader if the DFU flag is set (clearing it). Called early in boot.
void stm32_DfuCheckAndJump();
#endif
