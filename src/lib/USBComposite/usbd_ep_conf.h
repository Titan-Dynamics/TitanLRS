/**
  ******************************************************************************
  * @file    usbd_ep_conf.h
  * @brief   USB endpoint configuration for the TitanLRS composite device.
  *
  * Replaces the single-class endpoint map from the stm32duino USBDevice library.
  * The device exposes two functions on one cable:
  *
  *   function 0 — CDC-ACM, interfaces 0 (comm) + 1 (data).
  *                Unchanged from the stock single-CDC device: this is `Serial`,
  *                carrying MAVLink / CRSF / the debug log. Any GCS still sees
  *                exactly one virtual COM port.
  *
  *   function 1 — CDC-NCM, interfaces 2 (comm) + 3 (data).
  *                A USB network adapter (lib/USBNet runs lwIP over it). The
  *                config API is HTTP on it and MAVLink is offered as UDP. The OS
  *                binds its own NCM driver, so it never shows up as a port.
  ******************************************************************************
  */

#ifndef __USBD_EP_CONF_H
#define __USBD_EP_CONF_H

#ifdef USBCON

#include <stdint.h>
#include "usbd_def.h"

#if defined(USB) && !defined(USB_OTG_FS) && !defined(USB_OTG_HS)
#error "USBComposite supports only OTG USB cores (dedicated FIFOs), not the PMA-based USB peripheral."
#endif

typedef struct {
  uint32_t ep_adress; /* Endpoint address */
  uint32_t ep_size;   /* FIFO size, in 32-bit words */
} ep_desc_t;

/* CDC-ACM function — same endpoint addresses as the stock stm32duino device */
#define CDC_OUT_EP                    0x01U
#define CDC_IN_EP                     0x81U
#define CDC_CMD_EP                    0x82U

/* CDC-NCM function */
#define NCM_OUT_EP                    0x03U
#define NCM_IN_EP                     0x83U
#define NCM_NOTIF_EP                  0x84U

/* Device endpoints number including EP0. Highest EP number in use is 4. */
#define DEV_NUM_EP                    0x05U

/* Interface numbering (must match the order in the configuration descriptor) */
#define CDC_COMM_ITF                  0x00U
#define CDC_DATA_ITF                  0x01U
#define NCM_COMM_ITF                  0x02U
#define NCM_DATA_ITF                  0x03U

/* String descriptor indices for the per-function names. 0..5 are reserved by the
 * core (langid, mfc, product, serial, config, interface). */
#define USBD_IDX_CDC_STR              0x06U
#define USBD_IDX_NCM_STR              0x07U
#define USBD_IDX_NCM_MAC_STR          0x08U

#ifdef USE_USB_HS
  #define CDC_DATA_MAX_PACKET_SIZE    USB_HS_MAX_PACKET_SIZE
  #define NCM_DATA_MAX_PACKET_SIZE    USB_HS_MAX_PACKET_SIZE
#else
  #define CDC_DATA_MAX_PACKET_SIZE    USB_FS_MAX_PACKET_SIZE
  #define NCM_DATA_MAX_PACKET_SIZE    USB_FS_MAX_PACKET_SIZE
#endif
#define CDC_CMD_PACKET_SIZE           8U
#define NCM_NOTIF_PACKET_SIZE         64U /* notifications are 8-16 bytes: always a short packet */

extern const ep_desc_t ep_def[];
extern const uint32_t ep_def_count;

#endif /* USBCON */
#endif /* __USBD_EP_CONF_H */
