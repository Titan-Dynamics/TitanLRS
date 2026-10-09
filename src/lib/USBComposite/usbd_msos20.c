/**
  ******************************************************************************
  * @file    usbd_msos20.c
  * @brief   BOS + Microsoft OS 2.0 descriptors. See usbd_msos20.h.
  ******************************************************************************
  */

#ifdef USBCON

#include "usbd_msos20.h"
#include "usbd_ep_conf.h"

/* Sizes are spelled out because each container has to declare the length of
 * everything nested inside it, and a mismatch fails silently on Windows (the
 * device enumerates, the NCM driver just never binds). */
#define MSOS20_SET_HEADER_LEN         10U
#define MSOS20_SUBSET_CONFIG_LEN       8U
#define MSOS20_SUBSET_FUNCTION_LEN     8U
#define MSOS20_COMPATIBLE_ID_LEN      20U

#define MSOS20_FUNCTION_SUBSET_TOTAL  (MSOS20_SUBSET_FUNCTION_LEN + \
                                       MSOS20_COMPATIBLE_ID_LEN)                /* 28 */
#define MSOS20_CONFIG_SUBSET_TOTAL    (MSOS20_SUBSET_CONFIG_LEN + \
                                       MSOS20_FUNCTION_SUBSET_TOTAL)            /* 36 */
#define MSOS20_SET_TOTAL              (MSOS20_SET_HEADER_LEN + \
                                       MSOS20_CONFIG_SUBSET_TOTAL)              /* 46 */

/* Windows 8.1. Older versions do not implement MS OS 2.0 at all. */
#define MSOS20_WINDOWS_VERSION        0x06030000U

#define U16(v)  (uint8_t)((v) & 0xFFU), (uint8_t)(((v) >> 8) & 0xFFU)
#define U32(v)  (uint8_t)((v) & 0xFFU), (uint8_t)(((v) >> 8) & 0xFFU), \
                (uint8_t)(((v) >> 16) & 0xFFU), (uint8_t)(((v) >> 24) & 0xFFU)


const uint8_t USBD_BOSDescriptor[] = {
  /* BOS descriptor header */
  0x05,                                 /* bLength */
  USB_DESC_TYPE_BOS,                    /* bDescriptorType */
  U16(5U + 28U),                        /* wTotalLength */
  0x01,                                 /* bNumDeviceCaps */

  /* Microsoft OS 2.0 platform capability */
  0x1C,                                 /* bLength: 4 + 16 UUID + 8 */
  USB_DEVICE_CAPABITY_TYPE,             /* bDescriptorType: DEVICE CAPABILITY */
  0x05,                                 /* bDevCapabilityType: PLATFORM */
  0x00,                                 /* bReserved */
  /* MS_OS_20_Platform_Capability_ID {D8DD60DF-4589-4CC7-9CD2-659D9E648A9F},
   * encoded the way UUIDs go on the wire: first three groups little-endian. */
  0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
  0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
  U32(MSOS20_WINDOWS_VERSION),          /* dwWindowsVersion */
  U16(MSOS20_SET_TOTAL),                /* wMSOSDescriptorSetTotalLength */
  USBD_MSOS20_VENDOR_CODE,              /* bMS_VendorCode */
  0x00,                                 /* bAltEnumCode: no alternate enumeration */
};

const uint16_t USBD_BOSDescriptor_len = (uint16_t)sizeof(USBD_BOSDescriptor);

const uint8_t USBD_MSOS20_DescriptorSet[] = {
  /* Set header */
  U16(MSOS20_SET_HEADER_LEN),           /* wLength */
  U16(0x0000),                          /* wDescriptorType: SET_HEADER_DESCRIPTOR */
  U32(MSOS20_WINDOWS_VERSION),          /* dwWindowsVersion */
  U16(MSOS20_SET_TOTAL),                /* wTotalLength */

  /* Configuration subset — bConfigurationValue here is an *index*, so 0 means
   * our one and only configuration (whose bConfigurationValue is 1). */
  U16(MSOS20_SUBSET_CONFIG_LEN),        /* wLength */
  U16(0x0001),                          /* wDescriptorType: SUBSET_HEADER_CONFIGURATION */
  0x00,                                 /* bConfigurationValue (index) */
  0x00,                                 /* bReserved */
  U16(MSOS20_CONFIG_SUBSET_TOTAL),      /* wTotalLength */

  /* Function subset — the NCM function only. The CDC-ACM function is left
   * alone so usbser.sys keeps binding it as a normal COM port. */
  U16(MSOS20_SUBSET_FUNCTION_LEN),      /* wLength */
  U16(0x0002),                          /* wDescriptorType: SUBSET_HEADER_FUNCTION */
  NCM_COMM_ITF,                         /* bFirstInterface */
  0x00,                                 /* bReserved */
  U16(MSOS20_FUNCTION_SUBSET_TOTAL),    /* wSubsetLength */

  /* Compatible ID: bind the inbox UsbNcm.sys host driver. Without it Windows 10
   * leaves an NCM function unbound ("NCM Gadget" with no driver); Windows 11
   * matches the class itself but honours this too. */
  U16(MSOS20_COMPATIBLE_ID_LEN),        /* wLength */
  U16(0x0003),                          /* wDescriptorType: FEATURE_COMPATIBLE_ID */
  'W', 'I', 'N', 'N', 'C', 'M', 0x00, 0x00,   /* CompatibleID */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* SubCompatibleID */
};

const uint16_t USBD_MSOS20_DescriptorSet_len = (uint16_t)sizeof(USBD_MSOS20_DescriptorSet);

/* Every nested length has to agree with what we actually emitted, or Windows
 * quietly declines to bind the driver and the failure looks like "the adapter
 * never appears". Catch it here instead. */
_Static_assert(sizeof(USBD_MSOS20_DescriptorSet) == MSOS20_SET_TOTAL,
               "MS OS 2.0 descriptor set length does not match its declared wTotalLength");
_Static_assert(sizeof(USBD_BOSDescriptor) == 5U + 28U,
               "BOS descriptor length does not match its declared wTotalLength");

#endif /* USBCON */
