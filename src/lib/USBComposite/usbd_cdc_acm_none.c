/**
  ******************************************************************************
  * @file    usbd_cdc_acm_none.c
  * @brief   The CDC-ACM hooks and port API on a family without the function
  *          (USB_HAS_CDC_ACM 0). Serial stays linked but never connects, so writes
  *          are dropped and reads find nothing.
  ******************************************************************************
  */

#ifdef USBCON

#include "usbd_cdc_acm.h"
#include "usbd_ctlreq.h"

#if !USB_HAS_CDC_ACM

CDC_TransmitQueue_TypeDef TransmitQueue;
CDC_ReceiveQueue_TypeDef ReceiveQueue;
__IO bool dtrState = false;
__IO bool rtsState = false;

uint8_t ACM_ClassInit(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);
  return (uint8_t)USBD_OK;
}

void ACM_ClassDeInit(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);
}

uint8_t ACM_ClassSetup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_CtlError(pdev, req);
  return (uint8_t)USBD_FAIL;
}

void ACM_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);
}

void ACM_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(pdev);
  UNUSED(epnum);
}

void ACM_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(pdev);
  UNUSED(epnum);
}

bool CDC_connected(void)
{
  return false;
}

void CDC_continue_transmit(void)
{
}

bool CDC_resume_receive(void)
{
  return false;
}

void CDC_enableDTR(bool enable)
{
  if (!enable) {
    dtrState = true;
  }
}

#endif /* !USB_HAS_CDC_ACM */
#endif /* USBCON */
