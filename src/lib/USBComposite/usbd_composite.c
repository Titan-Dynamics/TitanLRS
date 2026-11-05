/**
  ******************************************************************************
  * @file    usbd_composite.c
  * @brief   CDC-ACM + CDC-NCM composite USB device class. See the header.
  ******************************************************************************
  */

#ifdef USBCON

#include <stdbool.h>

#include "usbd_composite.h"
#include "usbd_msos20.h"
#include "usbd_ncm.h"
#include "usbd_cdc_acm.h"
#include "usbd_ctlreq.h"

/* Configuration descriptor ------------------------------------------------- */

#define CDC_FUNCTION_DESC_SIZ   (USB_HAS_CDC_ACM ? 66U : 0U)   /* IAD + 2 interfaces + functional + 3 EPs */
#define USBD_COMPOSITE_NUM_ITF  (USB_HAS_CDC_ACM ? 0x04U : 0x02U)
#define NCM_FUNCTION_DESC_SIZ    85U   /* IAD + comm (+ functional, notif EP) + data alt 0/1 + 2 EPs */
#define USBD_COMPOSITE_CFG_DESC_SIZ (9U + CDC_FUNCTION_DESC_SIZ + NCM_FUNCTION_DESC_SIZ)

__ALIGN_BEGIN static uint8_t USBD_Composite_CfgDesc[USBD_COMPOSITE_CFG_DESC_SIZ] __ALIGN_END = {
  /* Configuration descriptor */
  0x09,                                       /* bLength */
  USB_DESC_TYPE_CONFIGURATION,                /* bDescriptorType */
  LOBYTE(USBD_COMPOSITE_CFG_DESC_SIZ),        /* wTotalLength */
  HIBYTE(USBD_COMPOSITE_CFG_DESC_SIZ),
  USBD_COMPOSITE_NUM_ITF,                     /* bNumInterfaces: [2 CDC-ACM +] 2 CDC-NCM */
  0x01,                                       /* bConfigurationValue */
  0x00,                                       /* iConfiguration */
#if (USBD_SELF_POWERED == 1U)
  0xC0,                                       /* bmAttributes: self powered */
#else
  0x80,                                       /* bmAttributes: bus powered */
#endif
  USBD_MAX_POWER,                             /* bMaxPower (2 mA units) */

#if USB_HAS_CDC_ACM
  /* ---------------- CDC-ACM function (interfaces 0 + 1) ------------------- */

  /* Interface association: without this Windows treats the whole device as one
   * communications class instance and never binds the network function. */
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

#endif

  /* ---------------- CDC-NCM function (interfaces 2 + 3, or 0 + 1) --------- */

  /* Interface association. Kept on an NCM-only device too, which leaves the
   * device descriptor's IAD class codes valid. */
  0x08,                                       /* bLength */
  USB_DESC_TYPE_IAD,                          /* bDescriptorType */
  NCM_COMM_ITF,                               /* bFirstInterface */
  0x02,                                       /* bInterfaceCount */
  0x02,                                       /* bFunctionClass: Communications */
  0x0D,                                       /* bFunctionSubClass: NCM */
  0x00,                                       /* bFunctionProtocol */
  USBD_IDX_NCM_STR,                           /* iFunction */

  /* Communication interface */
  0x09,                                       /* bLength */
  USB_DESC_TYPE_INTERFACE,                    /* bDescriptorType */
  NCM_COMM_ITF,                               /* bInterfaceNumber */
  0x00,                                       /* bAlternateSetting */
  0x01,                                       /* bNumEndpoints */
  0x02,                                       /* bInterfaceClass: Communications */
  0x0D,                                       /* bInterfaceSubClass: NCM */
  0x00,                                       /* bInterfaceProtocol */
  USBD_IDX_NCM_STR,                           /* iInterface */

  /* Header functional descriptor (CDC 1.10) */
  0x05, 0x24, 0x00, 0x10, 0x01,
  /* Union functional descriptor */
  0x05, 0x24, 0x06, NCM_COMM_ITF, NCM_DATA_ITF,
  /* Ethernet networking functional descriptor */
  0x0D,                                       /* bFunctionLength */
  0x24,                                       /* bDescriptorType: CS_INTERFACE */
  0x0F,                                       /* bDescriptorSubtype: Ethernet Networking */
  USBD_IDX_NCM_MAC_STR,                       /* iMACAddress: the host end's address */
  0x00, 0x00, 0x00, 0x00,                     /* bmEthernetStatistics: none */
  LOBYTE(NCM_MAX_SEGMENT_SIZE),               /* wMaxSegmentSize */
  HIBYTE(NCM_MAX_SEGMENT_SIZE),
  0x00, 0x00,                                 /* wNumberMCFilters */
  0x00,                                       /* bNumberPowerFilters */
  /* NCM functional descriptor */
  0x06,                                       /* bFunctionLength */
  0x24,                                       /* bDescriptorType: CS_INTERFACE */
  0x1A,                                       /* bDescriptorSubtype: NCM */
  0x00, 0x01,                                 /* bcdNcmVersion 1.00 */
  /* bmNetworkCapabilities: SetEthernetPacketFilter (D0). With 0x00 macOS switches the data interface
   * back to alternate setting 0 on about half of all connections and never re-enables it, at least
   * when NCM is the only function (Apple r.170072016, DTS workaround in developer forums thread
   * 824727). The request is accepted and ignored in usbd_ncm.c. */
  0x01,

  /* Notification endpoint */
  0x07, USB_DESC_TYPE_ENDPOINT, NCM_NOTIF_EP, 0x03,
  LOBYTE(NCM_NOTIF_PACKET_SIZE), HIBYTE(NCM_NOTIF_PACKET_SIZE), 0x10,

  /* Data interface, alternate setting 0: no endpoints (link down) */
  0x09,                                       /* bLength */
  USB_DESC_TYPE_INTERFACE,                    /* bDescriptorType */
  NCM_DATA_ITF,                               /* bInterfaceNumber */
  0x00,                                       /* bAlternateSetting */
  0x00,                                       /* bNumEndpoints */
  0x0A,                                       /* bInterfaceClass: CDC Data */
  0x00,                                       /* bInterfaceSubClass */
  0x01,                                       /* bInterfaceProtocol: NTB */
  0x00,                                       /* iInterface */

  /* Data interface, alternate setting 1: the bulk pair (link up) */
  0x09,                                       /* bLength */
  USB_DESC_TYPE_INTERFACE,                    /* bDescriptorType */
  NCM_DATA_ITF,                               /* bInterfaceNumber */
  0x01,                                       /* bAlternateSetting */
  0x02,                                       /* bNumEndpoints */
  0x0A,                                       /* bInterfaceClass: CDC Data */
  0x00,                                       /* bInterfaceSubClass */
  0x01,                                       /* bInterfaceProtocol: NTB */
  0x00,                                       /* iInterface */

  /* Data OUT / IN */
  0x07, USB_DESC_TYPE_ENDPOINT, NCM_OUT_EP, 0x02,
  LOBYTE(NCM_DATA_MAX_PACKET_SIZE), HIBYTE(NCM_DATA_MAX_PACKET_SIZE), 0x00,
  0x07, USB_DESC_TYPE_ENDPOINT, NCM_IN_EP, 0x02,
  LOBYTE(NCM_DATA_MAX_PACKET_SIZE), HIBYTE(NCM_DATA_MAX_PACKET_SIZE), 0x00,
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

/* Class data for the core's handle, which nothing reads: each function keeps its own state. */
static uint8_t s_classData;

/* Class callbacks ----------------------------------------------------------- */

static uint8_t USBD_Composite_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  const uint8_t ret = ACM_ClassInit(pdev);
  if (ret != (uint8_t)USBD_OK) {
    return ret;
  }

  /* --- NCM: notification endpoint now, data endpoints on SET_INTERFACE(alt 1) --- */
  NCM_ClassInit(pdev);

  pdev->pClassDataCmsit[0] = (void *)&s_classData;
  pdev->pClassData = (void *)&s_classData;

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_Composite_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  ACM_ClassDeInit(pdev);
  NCM_ClassDeInit(pdev);

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
  uint8_t ifalt = 0U;
  uint16_t status_info = 0U;
  USBD_StatusTypeDef ret = USBD_OK;


  switch (req->bmRequest & USB_REQ_TYPE_MASK) {
    case USB_REQ_TYPE_VENDOR:
      return USBD_Composite_VendorReq(pdev, req);

    case USB_REQ_TYPE_CLASS:
      if (LOBYTE(req->wIndex) == NCM_COMM_ITF) {
        return NCM_ClassSetup(pdev, req);
      }
      return ACM_ClassSetup(pdev, req);

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
            /* Only the NCM data interface has an alternate setting. */
            static uint8_t alt;
            alt = (LOBYTE(req->wIndex) == NCM_DATA_ITF) ? NCM_GetAltSetting() : ifalt;
            (void)USBD_CtlSendData(pdev, &alt, 1U);
          } else {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state != USBD_STATE_CONFIGURED) {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          } else if (LOBYTE(req->wIndex) == NCM_DATA_ITF) {
            /* Alternate setting 1 is the host bringing the network link up. */
            if (NCM_SetAltSetting(pdev, LOBYTE(req->wValue)) != (uint8_t)USBD_OK) {
              USBD_CtlError(pdev, req);
              ret = USBD_FAIL;
            }
          } else if (LOBYTE(req->wValue) != 0U) {
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
   * An NCM transfer block can be any length, so this matters for it too. */
  const bool needsZlp =
    (pdev->ep_in[ep].total_length > 0U) &&
    ((pdev->ep_in[ep].total_length % hpcd->IN_ep[ep].maxpacket) == 0U);

  if (needsZlp) {
    pdev->ep_in[ep].total_length = 0U;
    (void)USBD_LL_Transmit(pdev, epnum, NULL, 0U);
    return (uint8_t)USBD_OK;
  }

  if ((ep == (NCM_IN_EP & 0x0FU)) || (ep == (NCM_NOTIF_EP & 0x0FU))) {
    NCM_DataIn(pdev, epnum);
  } else {
    ACM_DataIn(pdev, epnum);
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_Composite_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  const uint8_t ep = epnum & 0x0FU;

  if (ep == (NCM_OUT_EP & 0x0FU)) {
    NCM_DataOut(pdev, epnum);
  } else {
    ACM_DataOut(pdev, epnum);
  }

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_Composite_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  if (NCM_EP0_RxReady(pdev)) {
    return (uint8_t)USBD_OK;
  }

  ACM_EP0_RxReady(pdev);

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

#endif /* USBCON */
