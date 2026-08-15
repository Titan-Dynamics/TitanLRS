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
  *   function 1 — vendor class (0xFF), interface 2, a bulk pair.
  *                The config transport (lib/USBConfig), reached from the browser
  *                with WebUSB and from python with pyusb. The OS creates no
  *                serial node for it, so it cannot appear in — or be taken by —
  *                anything that enumerates COM ports.
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

/* Vendor (config) function */
#define VCFG_OUT_EP                   0x03U
#define VCFG_IN_EP                    0x83U

/* Device endpoints number including EP0. Highest EP number in use is 3. */
#define DEV_NUM_EP                    0x04U

/* Interface numbering (must match the order in the configuration descriptor) */
#define CDC_COMM_ITF                  0x00U
#define CDC_DATA_ITF                  0x01U
#define VCFG_ITF                      0x02U

/* String descriptor indices for the per-function names. 0..5 are reserved by the
 * core (langid, mfc, product, serial, config, interface). */
#define USBD_IDX_CDC_STR              0x06U
#define USBD_IDX_VCFG_STR             0x07U

#ifdef USE_USB_HS
  #define CDC_DATA_MAX_PACKET_SIZE    USB_HS_MAX_PACKET_SIZE
  #define VCFG_MAX_PACKET_SIZE        USB_HS_MAX_PACKET_SIZE
#else
  #define CDC_DATA_MAX_PACKET_SIZE    USB_FS_MAX_PACKET_SIZE
  #define VCFG_MAX_PACKET_SIZE        USB_FS_MAX_PACKET_SIZE
#endif
#define CDC_CMD_PACKET_SIZE           8U

extern const ep_desc_t ep_def[];
extern const uint32_t ep_def_count;

#endif /* USBCON */
#endif /* __USBD_EP_CONF_H */
