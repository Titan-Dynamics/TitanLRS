/**
  ******************************************************************************
  * @file    usbd_composite.c
  * @brief   CDC-ACM + vendor-class composite USB device class. See the header.
  *
  * The CDC half is derived from ST's usbd_cdc.c.
  * Copyright (c) 2015 STMicroelectronics, licensed under the terms in that file.
  ******************************************************************************
  */

#ifdef USBCON

#include <stdbool.h>

#include "usbd_composite.h"
#include "usbd_msos20.h"
#include "usbd_ctlreq.h"

/* Configuration descriptor ------------------------------------------------- */

#define CDC_FUNCTION_DESC_SIZ    66U   /* IAD + 2 interfaces + functional + 3 EPs */
#define VCFG_FUNCTION_DESC_SIZ   23U   /* 1 interface + 2 EPs */
#define USBD_COMPOSITE_CFG_DESC_SIZ (9U + CDC_FUNCTION_DESC_SIZ + VCFG_FUNCTION_DESC_SIZ)

__ALIGN_BEGIN static uint8_t USBD_Composite_CfgDesc[USBD_COMPOSITE_CFG_DESC_SIZ] __ALIGN_END = {
  /* Configuration descriptor */
  0x09,                                       /* bLength */
  USB_DESC_TYPE_CONFIGURATION,                /* bDescriptorType */
  LOBYTE(USBD_COMPOSITE_CFG_DESC_SIZ),        /* wTotalLength */
  HIBYTE(USBD_COMPOSITE_CFG_DESC_SIZ),
  0x03,                                       /* bNumInterfaces: 2 CDC + 1 vendor */
  0x01,                                       /* bConfigurationValue */
  0x00,                                       /* iConfiguration */
#if (USBD_SELF_POWERED == 1U)
  0xC0,                                       /* bmAttributes: self powered */
#else
  0x80,                                       /* bmAttributes: bus powered */
#endif
  USBD_MAX_POWER,                             /* bMaxPower (2 mA units) */

  /* ---------------- CDC-ACM function (interfaces 0 + 1) ------------------- */

  /* Interface association: without this Windows treats the whole device as one
   * communications class instance and never binds the vendor function. */
  0x08,                                       /* bLength */
  USB_DESC_TYPE_IAD,                          /* bDescriptorType */
  CDC_COMM_ITF,                               /* bFirstInterface */
  0x02,                                       /* bInterfaceCount */
  0x02,                                       /* bFunctionClass: Communications */
  0x02,                                       /* bFunctionSubClass: ACM */
  0x01,                                       /* bFunctionProtocol: AT commands */
  USBD_IDX_CDC_STR,                           /* iFunction */

  /* Communication interface */
  0x09,                                       /* bLength */
  USB_DESC_TYPE_INTERFACE,                    /* bDescriptorType */
  CDC_COMM_ITF,                               /* bInterfaceNumber */
  0x00,                                       /* bAlternateSetting */
  0x01,                                       /* bNumEndpoints */
  0x02,                                       /* bInterfaceClass: Communications */
  0x02,                                       /* bInterfaceSubClass: ACM */
  0x00,                                       /* bInterfaceProtocol */
  USBD_IDX_CDC_STR,                           /* iInterface */

  /* Header functional descriptor */
  0x05, 0x24, 0x00, 0x10, 0x01,
  /* Call management functional descriptor */
  0x05, 0x24, 0x01, 0x00, CDC_DATA_ITF,
  /* Abstract control management functional descriptor */
  0x04, 0x24, 0x02, 0x02,
  /* Union functional descriptor */
  0x05, 0x24, 0x06, CDC_COMM_ITF, CDC_DATA_ITF,

  /* Notification endpoint */
  0x07,                                       /* bLength */
  USB_DESC_TYPE_ENDPOINT,                     /* bDescriptorType */
  CDC_CMD_EP,                                 /* bEndpointAddress */
  0x03,                                       /* bmAttributes: Interrupt */
  LOBYTE(CDC_CMD_PACKET_SIZE),
  HIBYTE(CDC_CMD_PACKET_SIZE),
  CDC_FS_BINTERVAL,                           /* bInterval */

  /* Data interface */
  0x09,                                       /* bLength */
  USB_DESC_TYPE_INTERFACE,                    /* bDescriptorType */
  CDC_DATA_ITF,                               /* bInterfaceNumber */
  0x00,                                       /* bAlternateSetting */
  0x02,                                       /* bNumEndpoints */
  0x0A,                                       /* bInterfaceClass: CDC Data */
  0x00,                                       /* bInterfaceSubClass */
  0x00,                                       /* bInterfaceProtocol */
  USBD_IDX_CDC_STR,                           /* iInterface */

  /* Data OUT / IN */
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_OUT_EP, 0x02,
  LOBYTE(CDC_DATA_MAX_PACKET_SIZE), HIBYTE(CDC_DATA_MAX_PACKET_SIZE), 0x00,
  0x07, USB_DESC_TYPE_ENDPOINT, CDC_IN_EP, 0x02,
  LOBYTE(CDC_DATA_MAX_PACKET_SIZE), HIBYTE(CDC_DATA_MAX_PACKET_SIZE), 0x00,

  /* ---------------- Vendor config function (interface 2) ------------------ */

  /* One interface, no IAD: a single-interface function does not need one. */
  0x09,                                       /* bLength */
  USB_DESC_TYPE_INTERFACE,                    /* bDescriptorType */
  VCFG_ITF,                                   /* bInterfaceNumber */
  0x00,                                       /* bAlternateSetting */
  0x02,                                       /* bNumEndpoints */
  0xFF,                                       /* bInterfaceClass: Vendor specific */
  0x00,                                       /* bInterfaceSubClass */
  0x00,                                       /* bInterfaceProtocol */
  USBD_IDX_VCFG_STR,                          /* iInterface */

  /* Bulk OUT / IN */
  0x07, USB_DESC_TYPE_ENDPOINT, VCFG_OUT_EP, 0x02,
  LOBYTE(VCFG_MAX_PACKET_SIZE), HIBYTE(VCFG_MAX_PACKET_SIZE), 0x00,
  0x07, USB_DESC_TYPE_ENDPOINT, VCFG_IN_EP, 0x02,
  LOBYTE(VCFG_MAX_PACKET_SIZE), HIBYTE(VCFG_MAX_PACKET_SIZE), 0x00,
};

__ALIGN_BEGIN static uint8_t USBD_Composite_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END = {
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};

/* State --------------------------------------------------------------------- */

static USBD_CDC_HandleTypeDef  hcdc;
static USBD_VCFG_HandleTypeDef hvcfg;
static USBD_CDC_ItfTypeDef  *cdc_fops = NULL;
static USBD_VCFG_ItfTypeDef *vcfg_fops = NULL;

/* Class callbacks ----------------------------------------------------------- */

static uint8_t USBD_Composite_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  if ((cdc_fops == NULL) || (vcfg_fops == NULL)) {
    return (uint8_t)USBD_FAIL;
  }

  /* --- CDC --- */
  (void)USBD_LL_OpenEP(pdev, CDC_IN_EP, USBD_EP_TYPE_BULK, CDC_DATA_MAX_PACKET_SIZE);
  pdev->ep_in[CDC_IN_EP & 0x0FU].is_used = 1U;

  (void)USBD_LL_OpenEP(pdev, CDC_OUT_EP, USBD_EP_TYPE_BULK, CDC_DATA_MAX_PACKET_SIZE);
  pdev->ep_out[CDC_OUT_EP & 0x0FU].is_used = 1U;

  (void)USBD_LL_OpenEP(pdev, CDC_CMD_EP, USBD_EP_TYPE_INTR, CDC_CMD_PACKET_SIZE);
  pdev->ep_in[CDC_CMD_EP & 0x0FU].is_used = 1U;
  pdev->ep_in[CDC_CMD_EP & 0x0FU].bInterval =
    (pdev->dev_speed == USBD_SPEED_HIGH) ? CDC_HS_BINTERVAL : CDC_FS_BINTERVAL;

  hcdc.RxBuffer = NULL;
  hcdc.CmdOpCode = 0xFFU;
  (void)cdc_fops->Init();          /* hands us the receive buffer */
  hcdc.TxState = 0U;
  hcdc.RxState = 0U;

  if (hcdc.RxBuffer == NULL) {
    return (uint8_t)USBD_EMEM;
  }
  (void)USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, hcdc.RxBuffer, CDC_DATA_MAX_PACKET_SIZE);

  /* --- Vendor --- */
  (void)USBD_LL_OpenEP(pdev, VCFG_IN_EP, USBD_EP_TYPE_BULK, VCFG_MAX_PACKET_SIZE);
  pdev->ep_in[VCFG_IN_EP & 0x0FU].is_used = 1U;

  (void)USBD_LL_OpenEP(pdev, VCFG_OUT_EP, USBD_EP_TYPE_BULK, VCFG_MAX_PACKET_SIZE);
  pdev->ep_out[VCFG_OUT_EP & 0x0FU].is_used = 1U;

  hvcfg.RxBuffer = NULL;
  (void)vcfg_fops->Init();
  hvcfg.TxState = 0U;

  if (hvcfg.RxBuffer == NULL) {
    return (uint8_t)USBD_EMEM;
  }
  (void)USBD_LL_PrepareReceive(pdev, VCFG_OUT_EP, hvcfg.RxBuffer, VCFG_MAX_PACKET_SIZE);

  pdev->pClassDataCmsit[0] = (void *)&hcdc;
  pdev->pClassData = (void *)&hcdc;

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_Composite_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  (void)USBD_LL_CloseEP(pdev, CDC_IN_EP);
  pdev->ep_in[CDC_IN_EP & 0x0FU].is_used = 0U;
  (void)USBD_LL_CloseEP(pdev, CDC_OUT_EP);
  pdev->ep_out[CDC_OUT_EP & 0x0FU].is_used = 0U;
  (void)USBD_LL_CloseEP(pdev, CDC_CMD_EP);
  pdev->ep_in[CDC_CMD_EP & 0x0FU].is_used = 0U;
  pdev->ep_in[CDC_CMD_EP & 0x0FU].bInterval = 0U;

  (void)USBD_LL_CloseEP(pdev, VCFG_IN_EP);
  pdev->ep_in[VCFG_IN_EP & 0x0FU].is_used = 0U;
  (void)USBD_LL_CloseEP(pdev, VCFG_OUT_EP);
  pdev->ep_out[VCFG_OUT_EP & 0x0FU].is_used = 0U;

  if (cdc_fops != NULL) {
    (void)cdc_fops->DeInit();
  }
  if (vcfg_fops != NULL) {
    (void)vcfg_fops->DeInit();
  }

  pdev->pClassDataCmsit[0] = NULL;
  pdev->pClassData = NULL;

  return (uint8_t)USBD_OK;
}

/* Windows fetches the MS OS 2.0 descriptor set with a device-level vendor
 * request. The core routes class *and* vendor requests to the class Setup(). */
static uint8_t USBD_Composite_VendorReq(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  if ((req->bRequest == USBD_MSOS20_VENDOR_CODE) &&
      (req->wIndex == USBD_MSOS20_DESCRIPTOR_INDEX)) {
    uint16_t len = MIN(USBD_MSOS20_DescriptorSet_len, req->wLength);
    (void)USBD_CtlSendData(pdev, (uint8_t *)USBD_MSOS20_DescriptorSet, len);
    return (uint8_t)USBD_OK;
  }

  USBD_CtlError(pdev, req);
  return (uint8_t)USBD_FAIL;
}

static uint8_t USBD_Composite_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  uint16_t len;
  uint8_t ifalt = 0U;
  uint16_t status_info = 0U;
  USBD_StatusTypeDef ret = USBD_OK;

  if (cdc_fops == NULL) {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK) {
    case USB_REQ_TYPE_VENDOR:
      return USBD_Composite_VendorReq(pdev, req);

    case USB_REQ_TYPE_CLASS:
      /* The vendor function has no class requests, so anything addressed
       * outside the CDC interfaces is a host mistake. */
      if ((LOBYTE(req->wIndex) != CDC_COMM_ITF) && (LOBYTE(req->wIndex) != CDC_DATA_ITF)) {
        USBD_CtlError(pdev, req);
        return (uint8_t)USBD_FAIL;
      }

      if (req->wLength != 0U) {
        if ((req->bmRequest & 0x80U) != 0U) {
          (void)cdc_fops->Control(req->bRequest, (uint8_t *)hcdc.data, req->wLength);
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
        (void)cdc_fops->Control(req->bRequest, (uint8_t *)req, 0U);
      }
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest) {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED) {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          } else {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED) {
            (void)USBD_CtlSendData(pdev, &ifalt, 1U);
          } else {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state != USBD_STATE_CONFIGURED) {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

static uint8_t USBD_Composite_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;
  const uint8_t ep = epnum & 0x0FU;

  /* A transfer that is an exact multiple of the max packet size has to be
   * terminated with a zero-length packet, or the host keeps waiting for more.
   * This matters more for the vendor pipe than for CDC: WebUSB's transferIn()
   * asks for a length, and without the ZLP a 64-byte reply never completes. */
  const bool needsZlp =
    (pdev->ep_in[ep].total_length > 0U) &&
    ((pdev->ep_in[ep].total_length % hpcd->IN_ep[ep].maxpacket) == 0U);

  if (needsZlp) {
    pdev->ep_in[ep].total_length = 0U;
    (void)USBD_LL_Transmit(pdev, epnum, NULL, 0U);
    return (uint8_t)USBD_OK;
  }

  if (ep == (CDC_IN_EP & 0x0FU)) {
    hcdc.TxState = 0U;
    if ((cdc_fops != NULL) && (cdc_fops->TransmitCplt != NULL)) {
      (void)cdc_fops->TransmitCplt(hcdc.TxBuffer, &hcdc.TxLength, epnum);
    }
  } else if (ep == (VCFG_IN_EP & 0x0FU)) {
    hvcfg.TxState = 0U;
    if ((vcfg_fops != NULL) && (vcfg_fops->TransmitCplt != NULL)) {
      (void)vcfg_fops->TransmitCplt(hvcfg.TxBuffer, &hvcfg.TxLength);
    }
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_Composite_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  const uint8_t ep = epnum & 0x0FU;

  if (ep == (CDC_OUT_EP & 0x0FU)) {
    if (cdc_fops == NULL) {
      return (uint8_t)USBD_FAIL;
    }
    hcdc.RxLength = USBD_LL_GetRxDataSize(pdev, epnum);
    (void)cdc_fops->Receive(hcdc.RxBuffer, &hcdc.RxLength);
  } else if (ep == (VCFG_OUT_EP & 0x0FU)) {
    if (vcfg_fops == NULL) {
      return (uint8_t)USBD_FAIL;
    }
    hvcfg.RxLength = USBD_LL_GetRxDataSize(pdev, epnum);
    (void)vcfg_fops->Receive(hvcfg.RxBuffer, &hvcfg.RxLength);
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_Composite_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);

  /* Only the CDC function has an EP0 data stage. */
  if ((cdc_fops != NULL) && (hcdc.CmdOpCode != 0xFFU)) {
    (void)cdc_fops->Control(hcdc.CmdOpCode, (uint8_t *)hcdc.data, (uint16_t)hcdc.CmdLength);
    hcdc.CmdOpCode = 0xFFU;
  }

  return (uint8_t)USBD_OK;
}

/* Single-speed device: all three configuration getters return one descriptor. */
static uint8_t *USBD_Composite_GetCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_Composite_CfgDesc);
  return USBD_Composite_CfgDesc;
}

static uint8_t *USBD_Composite_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_Composite_DeviceQualifierDesc);
  return USBD_Composite_DeviceQualifierDesc;
}

USBD_ClassTypeDef USBD_Composite = {
  USBD_Composite_Init,
  USBD_Composite_DeInit,
  USBD_Composite_Setup,
  NULL,                             /* EP0_TxSent */
  USBD_Composite_EP0_RxReady,
  USBD_Composite_DataIn,
  USBD_Composite_DataOut,
  NULL,                             /* SOF */
  NULL,                             /* IsoINIncomplete */
  NULL,                             /* IsoOUTIncomplete */
  USBD_Composite_GetCfgDesc,
  USBD_Composite_GetCfgDesc,
  USBD_Composite_GetCfgDesc,
  USBD_Composite_GetDeviceQualifierDesc,
#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
  NULL,
#endif
};

/* Exported API -------------------------------------------------------------- */

uint8_t USBD_Composite_RegisterCDC(USBD_HandleTypeDef *pdev, USBD_CDC_ItfTypeDef *fops)
{
  UNUSED(pdev);
  if (fops == NULL) {
    return (uint8_t)USBD_FAIL;
  }
  cdc_fops = fops;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_Composite_RegisterVCFG(USBD_HandleTypeDef *pdev, USBD_VCFG_ItfTypeDef *fops)
{
  UNUSED(pdev);
  if (fops == NULL) {
    return (uint8_t)USBD_FAIL;
  }
  vcfg_fops = fops;
  return (uint8_t)USBD_OK;
}

USBD_CDC_HandleTypeDef *USBD_CDC_Handle(void)
{
  return &hcdc;
}

USBD_VCFG_HandleTypeDef *USBD_VCFG_Handle(void)
{
  return &hvcfg;
}

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length)
{
  UNUSED(pdev);
  hcdc.TxBuffer = pbuff;
  hcdc.TxLength = length;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff)
{
  UNUSED(pdev);
  hcdc.RxBuffer = pbuff;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev)
{
  if (hcdc.TxState != 0U) {
    return (uint8_t)USBD_BUSY;
  }
  hcdc.TxState = 1U;
  pdev->ep_in[CDC_IN_EP & 0x0FU].total_length = hcdc.TxLength;
  (void)USBD_LL_Transmit(pdev, CDC_IN_EP, hcdc.TxBuffer, hcdc.TxLength);
  return (uint8_t)USBD_OK;
}

uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  (void)USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, hcdc.RxBuffer, CDC_DATA_MAX_PACKET_SIZE);
  return (uint8_t)USBD_OK;
}

uint8_t USBD_CDC_ClearBuffer(USBD_HandleTypeDef *pdev)
{
  (void)USBD_LL_PrepareReceive(pdev, CDC_OUT_EP, NULL, 0U);
  return (uint8_t)USBD_OK;
}

uint8_t USBD_VCFG_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length)
{
  UNUSED(pdev);
  hvcfg.TxBuffer = pbuff;
  hvcfg.TxLength = length;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_VCFG_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff)
{
  UNUSED(pdev);
  hvcfg.RxBuffer = pbuff;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_VCFG_TransmitPacket(USBD_HandleTypeDef *pdev)
{
  if (hvcfg.TxState != 0U) {
    return (uint8_t)USBD_BUSY;
  }
  hvcfg.TxState = 1U;
  pdev->ep_in[VCFG_IN_EP & 0x0FU].total_length = hvcfg.TxLength;
  (void)USBD_LL_Transmit(pdev, VCFG_IN_EP, hvcfg.TxBuffer, hvcfg.TxLength);
  return (uint8_t)USBD_OK;
}

uint8_t USBD_VCFG_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  (void)USBD_LL_PrepareReceive(pdev, VCFG_OUT_EP, hvcfg.RxBuffer, VCFG_MAX_PACKET_SIZE);
  return (uint8_t)USBD_OK;
}

uint8_t USBD_VCFG_ClearBuffer(USBD_HandleTypeDef *pdev)
{
  (void)USBD_LL_PrepareReceive(pdev, VCFG_OUT_EP, NULL, 0U);
  return (uint8_t)USBD_OK;
}

#endif /* USBCON */
