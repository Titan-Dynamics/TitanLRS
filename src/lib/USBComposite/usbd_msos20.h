/**
  ******************************************************************************
  * @file    usbd_msos20.h
  * @brief   BOS + Microsoft OS 2.0 descriptors for the NCM (network) function.
  *
  * Windows 10 ships an NCM host driver (UsbNcm.sys) but does not bind it to an
  * NCM function by class alone. Rather than ship an .inf, the device asks for it:
  *
  *   1. The device descriptor reports bcdUSB >= 0x0201, so Windows asks for a
  *      Binary Object Store (BOS) descriptor.
  *   2. The BOS carries a Microsoft OS 2.0 platform capability, which names a
  *      vendor request code and the length of a descriptor set.
  *   3. Windows issues that vendor request (bmRequestType 0xC0, bRequest =
  *      USBD_MSOS20_VENDOR_CODE, wIndex = 7) and gets back a descriptor set
  *      whose function subset says "interfaces 2-3 are WINNCM".
  *
  * macOS and Linux bind NCM by class and ignore all of this.
  ******************************************************************************
  */

#ifndef __USBD_MSOS20_H
#define __USBD_MSOS20_H

#ifdef USBCON

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Vendor bRequest Windows uses to fetch the MS OS 2.0 descriptor set. Any value
 * that does not collide with our own vendor requests will do; we have none. */
#define USBD_MSOS20_VENDOR_CODE       0x20U

/* wIndex value for "give me the descriptor set" (MS_OS_20_DESCRIPTOR_INDEX) */
#define USBD_MSOS20_DESCRIPTOR_INDEX  0x07U

extern const uint8_t USBD_BOSDescriptor[];
extern const uint16_t USBD_BOSDescriptor_len;

extern const uint8_t USBD_MSOS20_DescriptorSet[];
extern const uint16_t USBD_MSOS20_DescriptorSet_len;

#ifdef __cplusplus
}
#endif

#endif /* USBCON */
#endif /* __USBD_MSOS20_H */
