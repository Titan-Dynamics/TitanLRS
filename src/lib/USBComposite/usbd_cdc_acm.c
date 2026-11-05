/**
  ******************************************************************************
  * @file    usbd_cdc_acm.c
  * @brief   CDC-ACM function of the TitanLRS composite device. See the header.
  *
  * Derived from ST's usbd_cdc.c and the stm32duino usbd_cdc_if.c.
  * Copyright (c) 2015 STMicroelectronics, licensed under the terms in those files.
  ******************************************************************************
  */

#ifdef USBCON

#include "usbd_cdc_acm.h"

#if USB_HAS_CDC_ACM

#include "usbd_composite_if.h"
#include "usbd_ctlreq.h"
#include "bootloader.h"

/*
 * How long a stalled IN transfer may sit before the port is treated as
 * disconnected, in HAL_GetTick() units (typically 1 ms), so writes stop blocking
 * when the host stops reading.
 */
#ifndef USB_CDC_TRANSMIT_TIMEOUT
  #define USB_CDC_TRANSMIT_TIMEOUT 3
#endif

#define CDC_REQ_MAX_DATA_SIZE                       0x7U

/* CDC class requests */
#define CDC_SEND_ENCAPSULATED_COMMAND               0x00U
#define CDC_GET_ENCAPSULATED_RESPONSE               0x01U
#define CDC_SET_COMM_FEATURE                        0x02U
#define CDC_GET_COMM_FEATURE                        0x03U
#define CDC_CLEAR_COMM_FEATURE                      0x04U
#define CDC_SET_LINE_CODING                         0x20U
#define CDC_GET_LINE_CODING                         0x21U
#define CDC_SET_CONTROL_LINE_STATE                  0x22U
#define CDC_SEND_BREAK                              0x23U

/* Control Line State bits */
#define CLS_DTR                                     (1 << 0)
#define CLS_RTS                                     (1 << 1)

/* --- state ---------------------------------------------------------------- */

static struct {
  uint32_t data[CDC_DATA_MAX_PACKET_SIZE / 4U]; /* forces 32-bit alignment */
  uint8_t  CmdOpCode;
  uint8_t  CmdLength;
  uint8_t  *RxBuffer;
  uint8_t  *TxBuffer;
  uint32_t RxLength;
  uint32_t TxLength;
  __IO uint32_t TxState;
  __IO uint32_t RxState;
} hcdc;

static bool CDC_DTR_enabled = true;
CDC_TransmitQueue_TypeDef TransmitQueue;
CDC_ReceiveQueue_TypeDef ReceiveQueue;
__IO bool dtrState = false; /* lineState */
__IO bool rtsState = false;
static __IO bool receivePended = true;
static uint32_t transmitStart = 0;

static struct {
  uint32_t bitrate;
  uint8_t  format;
  uint8_t  paritytype;
  uint8_t  datatype;
} linecoding = {
  115200, /* baud rate */
  0x00,   /* stop bits-1 */
  0x00,   /* parity - none */
  0x08    /* nb. of bits 8 */
};

#ifdef DTR_TOGGLING_SEQ
  /* DTR toggling sequence management (host-driven reset into the bootloader) */
  extern void dtr_togglingHook(uint8_t *buf, uint32_t *len);
  static uint8_t dtr_toggling = 0;
#endif

/* --- media callbacks ------------------------------------------------------ */

static int8_t CDC_Itf_Control(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
  UNUSED(length);

  switch (cmd) {
    case CDC_SEND_ENCAPSULATED_COMMAND:
    case CDC_GET_ENCAPSULATED_RESPONSE:
    case CDC_SET_COMM_FEATURE:
    case CDC_GET_COMM_FEATURE:
    case CDC_CLEAR_COMM_FEATURE:
    case CDC_SEND_BREAK:
      break;

    /*******************************************************************************/
    /* Line Coding Structure                                                       */
    /*-----------------------------------------------------------------------------*/
    /* Offset | Field       | Size | Value  | Description                          */
    /* 0      | dwDTERate   |   4  | Number |Data terminal rate, in bits per second*/
    /* 4      | bCharFormat |   1  | Number | Stop bits                            */
    /* 5      | bParityType |  1   | Number | Parity                               */
    /* 6      | bDataBits  |   1   | Number Data bits (5, 6, 7, 8 or 16).          */
    /*******************************************************************************/
    case CDC_SET_LINE_CODING:
      linecoding.bitrate    = (uint32_t)(pbuf[0] | (pbuf[1] << 8) | \
                                         (pbuf[2] << 16) | (pbuf[3] << 24));
      linecoding.format     = pbuf[4];
      linecoding.paritytype = pbuf[5];
      linecoding.datatype   = pbuf[6];
      break;

    case CDC_GET_LINE_CODING:
      pbuf[0] = (uint8_t)(linecoding.bitrate);
      pbuf[1] = (uint8_t)(linecoding.bitrate >> 8);
      pbuf[2] = (uint8_t)(linecoding.bitrate >> 16);
      pbuf[3] = (uint8_t)(linecoding.bitrate >> 24);
      pbuf[4] = linecoding.format;
      pbuf[5] = linecoding.paritytype;
      pbuf[6] = linecoding.datatype;
      break;

    case CDC_SET_CONTROL_LINE_STATE:
      dtrState = (CDC_DTR_enabled) ? (((USBD_SetupReqTypedef *)pbuf)->wValue & CLS_DTR) : true;
      /* Reset the transmit timeout when the port is connected — but only while
       * nothing is in flight. transmitStart is what eventually makes
       * CDC_connected() false and releases USBSerial::write(); clearing it
       * under a stuck IN transfer re-arms that escape, and a host which
       * re-asserts DTR while the queue is backed up (Windows/Mission Planner
       * does this on open) can hold the write loop open indefinitely. */
      if (dtrState && (hcdc.TxState == 0U)) {
        transmitStart = 0;
      }
      rtsState = (((USBD_SetupReqTypedef *)pbuf)->wValue & CLS_RTS);
#ifdef DTR_TOGGLING_SEQ
      dtr_toggling++;
#endif
      break;

    default:
      break;
  }

  return ((int8_t)USBD_OK);
}

static int8_t CDC_Itf_Receive(uint8_t *Buf, uint32_t *Len)
{
#ifdef DTR_TOGGLING_SEQ
  if (dtr_toggling > 3) {
    dtr_togglingHook(Buf, Len);
    dtr_toggling = 0;
  }
#else
  UNUSED(Buf);
#endif
  /* The queue always holds a whole free block, so this cannot overflow */
  CDC_ReceiveQueue_CommitBlock(&ReceiveQueue, (uint16_t)(*Len));
  receivePended = false;
  if (!CDC_resume_receive()) {
    (void)USBD_LL_PrepareReceive(&hUSBD_Device_CDC, CDC_OUT_EP, NULL, 0U);
  }
  return ((int8_t)USBD_OK);
}

static int8_t CDC_Itf_TransmitCplt(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  UNUSED(Buf);
  UNUSED(Len);
  UNUSED(epnum);
  transmitStart = 0;
  CDC_TransmitQueue_CommitRead(&TransmitQueue);
  CDC_continue_transmit();
  return ((int8_t)USBD_OK);
}

/* --- class driver hooks ---------------------------------------------------- */

uint8_t ACM_ClassInit(USBD_HandleTypeDef *pdev)
{
  (void)USBD_LL_OpenEP(pdev, CDC_IN_EP, USBD_EP_TYPE_BULK, CDC_DATA_MAX_PACKET_SIZE);
  pdev->ep_in[CDC_IN_EP & 0x0FU].is_used = 1U;

  (void)USBD_LL_OpenEP(pdev, CDC_OUT_EP, USBD_EP_TYPE_BULK, CDC_DATA_MAX_PACKET_SIZE);
  pdev->ep_out[CDC_OUT_EP & 0x0FU].is_used = 1U;

  (void)USBD_LL_OpenEP(pdev, CDC_CMD_EP, USBD_EP_TYPE_INTR, CDC_CMD_PACKET_SIZE);
  pdev->ep_in[CDC_CMD_EP & 0x0FU].is_used = 1U;
  pdev->ep_in[CDC_CMD_EP & 0x0FU].bInterval =
    (pdev->dev_speed == USBD_SPEED_HIGH) ? CDC_HS_BINTERVAL : CDC_FS_BINTERVAL;

  hcdc.CmdOpCode = 0xFFU;
  CDC_TransmitQueue_Init(&TransmitQueue);
  CDC_ReceiveQueue_Init(&ReceiveQueue);
  receivePended = true;
  hcdc.RxBuffer = CDC_ReceiveQueue_ReserveBlock(&ReceiveQueue);
  hcdc.TxState = 0U;
  hcdc.RxState = 0U;

  if (hcdc.RxBuffer == NULL) {
    return (uint8_t)USBD_EMEM;
  }
  (void)USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, hcdc.RxBuffer, CDC_DATA_MAX_PACKET_SIZE);
  return (uint8_t)USBD_OK;
}

void ACM_ClassDeInit(USBD_HandleTypeDef *pdev)
{
  (void)USBD_LL_CloseEP(pdev, CDC_IN_EP);
  pdev->ep_in[CDC_IN_EP & 0x0FU].is_used = 0U;
  (void)USBD_LL_CloseEP(pdev, CDC_OUT_EP);
  pdev->ep_out[CDC_OUT_EP & 0x0FU].is_used = 0U;
  (void)USBD_LL_CloseEP(pdev, CDC_CMD_EP);
  pdev->ep_in[CDC_CMD_EP & 0x0FU].is_used = 0U;
  pdev->ep_in[CDC_CMD_EP & 0x0FU].bInterval = 0U;
}

uint8_t ACM_ClassSetup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  uint16_t len;

  if ((LOBYTE(req->wIndex) != CDC_COMM_ITF) && (LOBYTE(req->wIndex) != CDC_DATA_ITF)) {
    USBD_CtlError(pdev, req);
    return (uint8_t)USBD_FAIL;
  }

  if (req->wLength != 0U) {
    if ((req->bmRequest & 0x80U) != 0U) {
      (void)CDC_Itf_Control(req->bRequest, (uint8_t *)hcdc.data, req->wLength);
      len = MIN(CDC_REQ_MAX_DATA_SIZE, req->wLength);
      (void)USBD_CtlSendData(pdev, (uint8_t *)hcdc.data, len);
    } else {
      hcdc.CmdOpCode = req->bRequest;
      hcdc.CmdLength = (uint8_t)MIN(req->wLength, USB_MAX_EP0_SIZE);
      (void)USBD_CtlPrepareRx(pdev, (uint8_t *)hcdc.data, hcdc.CmdLength);
    }
  } else {
    /* No data stage: the media layer reads the request itself. This is how
     * SET_CONTROL_LINE_STATE delivers DTR/RTS. */
    (void)CDC_Itf_Control(req->bRequest, (uint8_t *)req, 0U);
  }

  return (uint8_t)USBD_OK;
}

void ACM_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);
  if (hcdc.CmdOpCode != 0xFFU) {
    (void)CDC_Itf_Control(hcdc.CmdOpCode, (uint8_t *)hcdc.data, (uint16_t)hcdc.CmdLength);
    hcdc.CmdOpCode = 0xFFU;
  }
}

void ACM_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(pdev);
  if ((epnum & 0x0FU) == (CDC_IN_EP & 0x0FU)) {
    hcdc.TxState = 0U;
    (void)CDC_Itf_TransmitCplt(hcdc.TxBuffer, &hcdc.TxLength, epnum);
  }
}

void ACM_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  if ((epnum & 0x0FU) == (CDC_OUT_EP & 0x0FU)) {
    hcdc.RxLength = USBD_LL_GetRxDataSize(pdev, epnum);
    (void)CDC_Itf_Receive(hcdc.RxBuffer, &hcdc.RxLength);
  }
}

/* --- port API ------------------------------------------------------------- */

bool CDC_connected(void)
{
  /* Copy to a local to avoid a double read - fix stm32duino #478 */
  uint32_t transmitTime = transmitStart;
  if (transmitTime) {
    transmitTime = HAL_GetTick() - transmitTime;
  }
  return ((hUSBD_Device_CDC.dev_state == USBD_STATE_CONFIGURED)
          && (transmitTime < USB_CDC_TRANSMIT_TIMEOUT)
          && dtrState);
}

void CDC_continue_transmit(void)
{
  uint16_t size;
  uint8_t *buffer;

  /*
   * TS: reachable from both the main thread (USBSerial::write) and the USB IRQ
   * (TransmitCplt), but the main thread cannot pass the TxState test while a
   * transfer is pending, and the IRQ cannot be preempted by the main thread.
   */
  if (hcdc.TxState == 0U) {
    buffer = CDC_TransmitQueue_ReadBlock(&TransmitQueue, &size);
    if (size > 0) {
      transmitStart = HAL_GetTick();
      hcdc.TxBuffer = buffer;
      hcdc.TxLength = size;
      hcdc.TxState = 1U;
      hUSBD_Device_CDC.ep_in[CDC_IN_EP & 0x0FU].total_length = hcdc.TxLength;
      (void)USBD_LL_Transmit(&hUSBD_Device_CDC, CDC_IN_EP, hcdc.TxBuffer, hcdc.TxLength);
    }
  }
}

bool CDC_resume_receive(void)
{
  /* TS: the main and IRQ threads cannot both pass this, because the IRQ only
   * fires while receivePended is true. */
  if (!receivePended) {
    uint8_t *block = CDC_ReceiveQueue_ReserveBlock(&ReceiveQueue);
    if (block != NULL) {
      receivePended = true;
      hcdc.RxBuffer = block;
      (void)USBD_LL_PrepareReceive(&hUSBD_Device_CDC, CDC_OUT_EP, hcdc.RxBuffer, CDC_DATA_MAX_PACKET_SIZE);
      return true;
    }
  }
  return false;
}
void CDC_enableDTR(bool enable)
{
  CDC_DTR_enabled = enable;
  if (!enable) {
    /* Disabling the gate has to take effect now, not at the next
     * SET_CONTROL_LINE_STATE. The stock version only cleared the override flag,
     * so dtrState stayed false — and CDC_connected() with it — until a host
     * happened to send that request. A host which opens the port without
     * asserting DTR, or never sends the request at all, therefore got a port
     * that opened but carried no data. */
    dtrState = true;
  }
}

#endif /* USB_HAS_CDC_ACM */
#endif /* USBCON */
