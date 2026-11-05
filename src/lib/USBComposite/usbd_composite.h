/**
  ******************************************************************************
  * @file    usbd_composite.h
  * @brief   CDC-ACM + CDC-NCM composite USB device class for TitanLRS.
  *
  * One USBD class driver presenting two functions:
  *
  *   CDC-ACM (usbd_cdc_acm.c) — `Serial`, unchanged from the stock stm32duino
  *   device. MAVLink, CRSF and the debug log. One COM port, as before. Left out
  *   on a family with USB_HAS_CDC_ACM 0 (usbnet_port_*.h).
  *
  *   CDC-NCM (usbd_ncm.c) — a USB network adapter. lib/USBNet runs lwIP on it:
  *   the HTTP config API and MAVLink over UDP. The OS binds its own network
  *   driver, so it never shows up as a port.
  *
  ******************************************************************************
  */

#ifndef __USBD_COMPOSITE_H
#define __USBD_COMPOSITE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ioreq.h"
#include "usbd_ep_conf.h"

extern USBD_ClassTypeDef USBD_Composite;
#define USBD_COMPOSITE_CLASS &USBD_Composite

#ifdef __cplusplus
}
#endif

#endif /* __USBD_COMPOSITE_H */
