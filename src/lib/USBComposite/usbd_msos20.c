/**
  ******************************************************************************
  * @file    usbd_msos20.c
  * @brief   BOS + Microsoft OS 2.0 descriptors. See usbd_msos20.h.
  ******************************************************************************
  */

#ifdef USBCON

#include "usbd_msos20.h"
#include "usbd_ep_conf.h"

/* Device interface GUID Windows registers for the vendor function. It is
 * arbitrary but must be stable — changing it orphans existing installs. */
#define VCFG_DEVICE_INTERFACE_GUID "{A9F0C1E4-3B7D-4E62-9C15-6D2F8B4A0E77}"

/* Sizes are spelled out because each container has to declare the length of
 * everything nested inside it, and a mismatch fails silently on Windows (the
 * device enumerates, WinUSB just never binds). */
#define MSOS20_SET_HEADER_LEN         10U
#define MSOS20_SUBSET_CONFIG_LEN       8U
#define MSOS20_SUBSET_FUNCTION_LEN     8U
#define MSOS20_COMPATIBLE_ID_LEN      20U
/* 2+2+2+2 fixed + 42 name ("DeviceInterfaceGUIDs\0" UTF-16LE)
 *                + 2      + 80 data (38-char GUID + NUL + MULTI_SZ terminator) */
#define MSOS20_REG_PROPERTY_LEN      132U

#define MSOS20_FUNCTION_SUBSET_TOTAL  (MSOS20_SUBSET_FUNCTION_LEN + \
                                       MSOS20_COMPATIBLE_ID_LEN + \
                                       MSOS20_REG_PROPERTY_LEN)                 /* 160 */
#define MSOS20_CONFIG_SUBSET_TOTAL    (MSOS20_SUBSET_CONFIG_LEN + \
                                       MSOS20_FUNCTION_SUBSET_TOTAL)            /* 168 */
#define MSOS20_SET_TOTAL              (MSOS20_SET_HEADER_LEN + \
                                       MSOS20_CONFIG_SUBSET_TOTAL)              /* 178 */

/* Windows 8.1. Older versions do not implement MS OS 2.0 at all. */
#define MSOS20_WINDOWS_VERSION        0x06030000U

#define U16(v)  (uint8_t)((v) & 0xFFU), (uint8_t)(((v) >> 8) & 0xFFU)
#define U32(v)  (uint8_t)((v) & 0xFFU), (uint8_t)(((v) >> 8) & 0xFFU), \
                (uint8_t)(((v) >> 16) & 0xFFU), (uint8_t)(((v) >> 24) & 0xFFU)

/* ASCII -> UTF-16LE, one char per pair. Only valid for 7-bit input. */
#define U16STR_2(a, b)              (a), 0, (b), 0
#define U16STR_4(a, b, c, d)        U16STR_2(a, b), U16STR_2(c, d)
#define U16STR_8(a, b, c, d, e, f, g, h) \
                                    U16STR_4(a, b, c, d), U16STR_4(e, f, g, h)

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

  /* Function subset — the vendor interface only. The CDC function is left
   * alone so usbser.sys keeps binding it as a normal COM port. */
  U16(MSOS20_SUBSET_FUNCTION_LEN),      /* wLength */
  U16(0x0002),                          /* wDescriptorType: SUBSET_HEADER_FUNCTION */
  VCFG_ITF,                             /* bFirstInterface */
  0x00,                                 /* bReserved */
  U16(MSOS20_FUNCTION_SUBSET_TOTAL),    /* wSubsetLength */

  /* Compatible ID: bind WinUSB */
  U16(MSOS20_COMPATIBLE_ID_LEN),        /* wLength */
  U16(0x0003),                          /* wDescriptorType: FEATURE_COMPATIBLE_ID */
  'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,   /* CompatibleID */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* SubCompatibleID */

  /* Registry property: DeviceInterfaceGUIDs (REG_MULTI_SZ) */
  U16(MSOS20_REG_PROPERTY_LEN),         /* wLength */
  U16(0x0004),                          /* wDescriptorType: FEATURE_REG_PROPERTY */
  U16(0x0007),                          /* wPropertyDataType: REG_MULTI_SZ */
  U16(42),                              /* wPropertyNameLength */
  U16STR_8('D', 'e', 'v', 'i', 'c', 'e', 'I', 'n'),
  U16STR_8('t', 'e', 'r', 'f', 'a', 'c', 'e', 'G'),
  U16STR_4('U', 'I', 'D', 's'),
  0x00, 0x00,                           /* name NUL */
  U16(80),                              /* wPropertyDataLength */
  U16STR_8('{', 'A', '9', 'F', '0', 'C', '1', 'E'),
  U16STR_8('4', '-', '3', 'B', '7', 'D', '-', '4'),
  U16STR_8('E', '6', '2', '-', '9', 'C', '1', '5'),
  U16STR_8('-', '6', 'D', '2', 'F', '8', 'B', '4'),
  U16STR_8('A', '0', 'E', '7', '7', '}', 0x00, 0x00),
};

const uint16_t USBD_MSOS20_DescriptorSet_len = (uint16_t)sizeof(USBD_MSOS20_DescriptorSet);

/* Every nested length has to agree with what we actually emitted, or Windows
 * quietly declines to bind WinUSB and the failure looks like "WebUSB can't see
 * the device". Catch it here instead. */
_Static_assert(sizeof(USBD_MSOS20_DescriptorSet) == MSOS20_SET_TOTAL,
               "MS OS 2.0 descriptor set length does not match its declared wTotalLength");
_Static_assert(sizeof(USBD_BOSDescriptor) == 5U + 28U,
               "BOS descriptor length does not match its declared wTotalLength");

#endif /* USBCON */
