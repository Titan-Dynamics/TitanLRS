#pragma once

/*
 * What differs between MCU families in the USB device and network stack. Each family's header is
 * the complete list of values, so comparing the two shows every difference. Nothing else in
 * lib/USBComposite or lib/USBNet tests a family macro (usbd_conf.c is stock STM32duino).
 *
 * Lives next to lwipopts.h because that directory is on the global include path.
 */
#if defined(STM32F4xx)
#include "usbnet_port_f4.h"
#elif defined(STM32H7xx)
#include "usbnet_port_h7.h"
#else
#error "No USB port header for this MCU family: add one with the values in usbnet_port_h7.h"
#endif
