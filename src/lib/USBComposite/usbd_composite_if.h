/**
  ******************************************************************************
  * @file    usbd_composite_if.h
  * @brief   Media layer for the TitanLRS composite device: a transmit/receive
  *          queue pair for the CDC port and another for the vendor config pipe,
  *          plus the device bring-up.
  *
  * The CDC half keeps the stm32duino names and signatures verbatim, so
  * USBSerial.cpp is unchanged apart from which function starts the device.
  ******************************************************************************
  */

#ifndef __USBD_COMPOSITE_IF_H
#define __USBD_COMPOSITE_IF_H

#ifdef USBCON

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_composite.h"
#include "cdc_queue.h"

extern USBD_HandleTypeDef hUSBD_Device_CDC;

/*
 * Hard ceiling on how long any USB write may block, in milliseconds.
 *
 * Both stream write() and flush() run from the main loop, which also services
 * the CRSF handset UART and the config pipe. A queue only drains when the host
 * picks up the IN transfer, so any "wait for space" loop is at the mercy of the
 * host — and a host that has stopped reading (a released WebUSB interface, a
 * COM port opened but not serviced) makes that wait unbounded. Losing the tail
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

/* --- CDC port (Serial) ---------------------------------------------------- */

extern CDC_TransmitQueue_TypeDef TransmitQueue;
extern CDC_ReceiveQueue_TypeDef ReceiveQueue;
extern __IO bool dtrState;
extern __IO bool rtsState;

bool CDC_connected(void);
void CDC_continue_transmit(void);
bool CDC_resume_receive(void);
void CDC_enableDTR(bool enable);

/* --- Vendor config pipe (WebUSB / libusb) --------------------------------- */

extern CDC_TransmitQueue_TypeDef VCFG_TransmitQueue;
extern CDC_ReceiveQueue_TypeDef VCFG_ReceiveQueue;

bool VCFG_connected(void);
void VCFG_continue_transmit(void);
bool VCFG_resume_receive(void);

#ifdef __cplusplus
}
#endif

#endif /* USBCON */
#endif /* __USBD_COMPOSITE_IF_H */
