/**
  ******************************************************************************
  * @file    usbd_cdc_acm.h
  * @brief   CDC-ACM function of the TitanLRS composite device: `Serial`, carrying
  *          MAVLink, CRSF and the debug log.
  *
  * Same shape as usbd_ncm.h: usbd_composite.c owns the device and calls the hooks
  * below. usbd_cdc_acm.c implements them, or usbd_cdc_acm_none.c on a family with
  * USB_HAS_CDC_ACM 0, where Serial stays linked but never connects.
  *
  * The port API keeps the stm32duino names and signatures, so USBSerial.cpp is
  * unchanged apart from which function starts the device.
  ******************************************************************************
  */

#ifndef __USBD_CDC_ACM_H
#define __USBD_CDC_ACM_H

#ifdef USBCON

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "usbd_ioreq.h"
#include "usbd_ep_conf.h"
#include "cdc_queue.h"

/* --- class driver hooks, called by usbd_composite.c (interrupt context) ------------------- */

uint8_t ACM_ClassInit(USBD_HandleTypeDef *pdev);
void    ACM_ClassDeInit(USBD_HandleTypeDef *pdev);
/* Class requests for an interface that is not NCM's. */
uint8_t ACM_ClassSetup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
/* EP0 OUT data stage of a class request that was not NCM's. */
void    ACM_EP0_RxReady(USBD_HandleTypeDef *pdev);
/* Transfer complete on an endpoint that is not NCM's. */
void    ACM_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
void    ACM_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);

/* --- port API (USBSerial.cpp) ------------------------------------------------------------- */

extern CDC_TransmitQueue_TypeDef TransmitQueue;
extern CDC_ReceiveQueue_TypeDef ReceiveQueue;
extern __IO bool dtrState;
extern __IO bool rtsState;

bool CDC_connected(void);
void CDC_continue_transmit(void);
bool CDC_resume_receive(void);
void CDC_enableDTR(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* USBCON */
#endif /* __USBD_CDC_ACM_H */
