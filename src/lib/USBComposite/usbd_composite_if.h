/**
  ******************************************************************************
  * @file    usbd_composite_if.h
  * @brief   Device bring-up for the TitanLRS composite device. The port API of the
  *          CDC-ACM function comes with it (usbd_cdc_acm.h).
  ******************************************************************************
  */

#ifndef __USBD_COMPOSITE_IF_H
#define __USBD_COMPOSITE_IF_H

#ifdef USBCON

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_composite.h"
#include "usbd_cdc_acm.h"

extern USBD_HandleTypeDef hUSBD_Device_CDC;

/*
 * Hard ceiling on how long any USB write may block, in milliseconds.
 *
 * Both stream write() and flush() run from the main loop, which also services
 * the CRSF handset UART and the network stack. A queue only drains when the host
 * picks up the IN transfer, so any "wait for space" loop is at the mercy of the
 * host — and a host that has stopped reading (a COM port opened but not
 * serviced) makes that wait unbounded. Losing the tail
 * of a USB write is recoverable; stalling the main loop is not: the handset
 * link drops, LUA stops answering and telemetry stops.
 *
 * A prompt host completes a <=128-byte block in about a millisecond, so this is
 * roughly fifty times the headroom a working transfer needs.
 */
#ifndef USB_TX_BLOCKING_LIMIT_MS
#define USB_TX_BLOCKING_LIMIT_MS 50U
#endif

/* Brings the whole composite device up / down. Both functions enumerate
 * together, so this is device-wide: idempotent, first call wins. */
void USBComposite_init(void);
void USBComposite_deInit(void);

#ifdef __cplusplus
}
#endif

#endif /* USBCON */
#endif /* __USBD_COMPOSITE_IF_H */
