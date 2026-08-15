/**
  ******************************************************************************
  * @file    usbd_composite_if.c
  * @brief   Media layer for the TitanLRS composite device.
  *
  * The CDC half is the stm32duino usbd_cdc_if.c unchanged in behaviour.
  * Copyright (c) 2015 STMicroelectronics.
  ******************************************************************************
  */

#ifdef USBCON

#include "usbd_desc.h"
#include "usbd_composite_if.h"
#include "bootloader.h"

/*
 * How long a stalled IN transfer may sit before the port is treated as
 * disconnected, in HAL_GetTick() units (typically 1 ms), so writes stop blocking
 * when the host stops reading.
 */
#ifndef USB_CDC_TRANSMIT_TIMEOUT
  #define USB_CDC_TRANSMIT_TIMEOUT 3
#endif

/*
 * The vendor pipe needs a far more generous one. A CDC host driver keeps a read
 * permanently pending, so an IN transfer is picked up immediately; a WebUSB or
 * libusb client only has data in flight while it has a transferIn() posted, and
 * there is a real gap between "device wrote the reply" and "page got round to
 * asking for it". At the CDC timeout that gap truncates replies. This is only a
 * deadlock escape — an unplug drops dev_state out of CONFIGURED, which ends the
 * write loop immediately regardless.
 */
#ifndef USB_VCFG_TRANSMIT_TIMEOUT
  #define USB_VCFG_TRANSMIT_TIMEOUT 1000
#endif

/* USB Device Core handle (one device, two functions) */
USBD_HandleTypeDef hUSBD_Device_CDC;

static bool USBComposite_initialized = false;
#if defined(ICACHE) && defined (HAL_ICACHE_MODULE_ENABLED) && !defined(HAL_ICACHE_MODULE_DISABLED)
  static bool icache_enabled = false;
#endif

/* --- CDC state ------------------------------------------------------------ */

static bool CDC_DTR_enabled = true;
CDC_TransmitQueue_TypeDef TransmitQueue;
CDC_ReceiveQueue_TypeDef ReceiveQueue;
__IO bool dtrState = false; /* lineState */
__IO bool rtsState = false;
static __IO bool receivePended = true;
static uint32_t transmitStart = 0;

static USBD_CDC_LineCodingTypeDef linecoding = {
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

/* --- Vendor state --------------------------------------------------------- */

CDC_TransmitQueue_TypeDef VCFG_TransmitQueue;
CDC_ReceiveQueue_TypeDef VCFG_ReceiveQueue;
static __IO bool vcfgReceivePended = true;
static uint32_t vcfgTransmitStart = 0;

/* --- CDC media callbacks -------------------------------------------------- */

static int8_t CDC_Itf_Init(void)
{
  CDC_TransmitQueue_Init(&TransmitQueue);
  CDC_ReceiveQueue_Init(&ReceiveQueue);
  receivePended = true;
  USBD_CDC_SetRxBuffer(&hUSBD_Device_CDC, CDC_ReceiveQueue_ReserveBlock(&ReceiveQueue));
  return ((int8_t)USBD_OK);
}

static int8_t CDC_Itf_DeInit(void)
{
  return ((int8_t)USBD_OK);
}

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
      if (dtrState && (USBD_CDC_Handle()->TxState == 0U)) {
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
    USBD_CDC_ClearBuffer(&hUSBD_Device_CDC);
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

static USBD_CDC_ItfTypeDef USBD_CDC_fops = {
  CDC_Itf_Init,
  CDC_Itf_DeInit,
  CDC_Itf_Control,
  CDC_Itf_Receive,
  CDC_Itf_TransmitCplt
};

/* --- Vendor media callbacks ----------------------------------------------- */

static int8_t VCFG_Itf_Init(void)
{
  CDC_TransmitQueue_Init(&VCFG_TransmitQueue);
  CDC_ReceiveQueue_Init(&VCFG_ReceiveQueue);
  vcfgReceivePended = true;
  USBD_VCFG_SetRxBuffer(&hUSBD_Device_CDC, CDC_ReceiveQueue_ReserveBlock(&VCFG_ReceiveQueue));
  return ((int8_t)USBD_OK);
}

static int8_t VCFG_Itf_DeInit(void)
{
  return ((int8_t)USBD_OK);
}

static int8_t VCFG_Itf_Receive(uint8_t *Buf, uint32_t *Len)
{
  UNUSED(Buf);
  CDC_ReceiveQueue_CommitBlock(&VCFG_ReceiveQueue, (uint16_t)(*Len));
  vcfgReceivePended = false;
  if (!VCFG_resume_receive()) {
    USBD_VCFG_ClearBuffer(&hUSBD_Device_CDC);
  }
  return ((int8_t)USBD_OK);
}

static int8_t VCFG_Itf_TransmitCplt(uint8_t *Buf, uint32_t *Len)
{
  UNUSED(Buf);
  UNUSED(Len);
  vcfgTransmitStart = 0;
  CDC_TransmitQueue_CommitRead(&VCFG_TransmitQueue);
  VCFG_continue_transmit();
  return ((int8_t)USBD_OK);
}

static USBD_VCFG_ItfTypeDef USBD_VCFG_fops = {
  VCFG_Itf_Init,
  VCFG_Itf_DeInit,
  VCFG_Itf_Receive,
  VCFG_Itf_TransmitCplt
};

/* --- Device bring-up ------------------------------------------------------ */

void USBComposite_init(void)
{
#if defined(ICACHE) && defined (HAL_ICACHE_MODULE_ENABLED) && !defined(HAL_ICACHE_MODULE_DISABLED)
  if (HAL_ICACHE_IsEnabled() == 1) {
    icache_enabled = true;
    /* Disable instruction cache prior to internal cacheable memory update */
    if (HAL_ICACHE_Disable() != HAL_OK) {
      Error_Handler();
    }
  }
#endif
  if (!USBComposite_initialized) {
    if (USBD_Init(&hUSBD_Device_CDC, &USBD_Desc, 0) == USBD_OK) {
      if (USBD_RegisterClass(&hUSBD_Device_CDC, USBD_COMPOSITE_CLASS) == USBD_OK) {
        /* Both media interfaces must be registered before Start: USBD_Start can
         * complete enumeration and call the class Init(), which uses them. */
        if ((USBD_Composite_RegisterCDC(&hUSBD_Device_CDC, &USBD_CDC_fops) == USBD_OK) &&
            (USBD_Composite_RegisterVCFG(&hUSBD_Device_CDC, &USBD_VCFG_fops) == USBD_OK)) {
          USBD_Start(&hUSBD_Device_CDC);
          USBComposite_initialized = true;
        }
      }
    }
  }
}

void USBComposite_deInit(void)
{
  if (USBComposite_initialized) {
    USBD_Stop(&hUSBD_Device_CDC);
    USBD_DeInit(&hUSBD_Device_CDC);
    USBComposite_initialized = false;
  }
#if defined(ICACHE) && defined (HAL_ICACHE_MODULE_ENABLED) && !defined(HAL_ICACHE_MODULE_DISABLED)
  if (icache_enabled) {
    if (HAL_ICACHE_Enable() != HAL_OK) {
      Error_Handler();
    }
  }
#endif
}

/* --- CDC flow control ----------------------------------------------------- */

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
  USBD_CDC_HandleTypeDef *hcdc = USBD_CDC_Handle();

  /*
   * TS: reachable from both the main thread (USBSerial::write) and the USB IRQ
   * (TransmitCplt), but the main thread cannot pass the TxState test while a
   * transfer is pending, and the IRQ cannot be preempted by the main thread.
   */
  if (hcdc->TxState == 0U) {
    buffer = CDC_TransmitQueue_ReadBlock(&TransmitQueue, &size);
    if (size > 0) {
      transmitStart = HAL_GetTick();
      USBD_CDC_SetTxBuffer(&hUSBD_Device_CDC, buffer, size);
      USBD_CDC_TransmitPacket(&hUSBD_Device_CDC);
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
      USBD_CDC_SetRxBuffer(&hUSBD_Device_CDC, block);
      USBD_CDC_ReceivePacket(&hUSBD_Device_CDC);
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

/* --- Vendor flow control -------------------------------------------------- */

bool VCFG_connected(void)
{
  uint32_t transmitTime = vcfgTransmitStart;
  if (transmitTime) {
    transmitTime = HAL_GetTick() - transmitTime;
  }
  /* No DTR equivalent on a vendor interface: being configured is as much as the
   * device can know. Whether anyone has claimed the interface is invisible to
   * it, so an unclaimed pipe simply backs up until the timeout above. */
  return ((hUSBD_Device_CDC.dev_state == USBD_STATE_CONFIGURED)
          && (transmitTime < USB_VCFG_TRANSMIT_TIMEOUT));
}

void VCFG_continue_transmit(void)
{
  uint16_t size;
  uint8_t *buffer;
  USBD_VCFG_HandleTypeDef *hvcfg = USBD_VCFG_Handle();

  if (hvcfg->TxState == 0U) {
    buffer = CDC_TransmitQueue_ReadBlock(&VCFG_TransmitQueue, &size);
    if (size > 0) {
      vcfgTransmitStart = HAL_GetTick();
      USBD_VCFG_SetTxBuffer(&hUSBD_Device_CDC, buffer, size);
      USBD_VCFG_TransmitPacket(&hUSBD_Device_CDC);
    }
  }
}

bool VCFG_resume_receive(void)
{
  if (!vcfgReceivePended) {
    uint8_t *block = CDC_ReceiveQueue_ReserveBlock(&VCFG_ReceiveQueue);
    if (block != NULL) {
      vcfgReceivePended = true;
      USBD_VCFG_SetRxBuffer(&hUSBD_Device_CDC, block);
      USBD_VCFG_ReceivePacket(&hUSBD_Device_CDC);
      return true;
    }
  }
  return false;
}

#endif /* USBCON */
