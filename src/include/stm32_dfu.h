#pragma once

#include "targets.h"

// Reboot into the MCU's ROM DFU bootloader. The request sets an RTC backup-register flag and
// resets; on the next boot stm32_DfuCheckAndJump() (called from initVariant(), before setup())
// sees the flag and jumps to the ROM bootloader, which enumerates as the standard USB DFU device.
// No custom bootloader and no flash-layout change: the ROM DFU cannot be corrupted, so a bad
// application image is always recoverable.

#if defined(PLATFORM_STM32)
/// Set the DFU flag and reset. Does not return.
void stm32_RequestDfu();

/// Jump to the ROM bootloader if the DFU flag is set (clearing it). Called early in boot.
void stm32_DfuCheckAndJump();
#endif
