/**
  ******************************************************************************
  * @file    usbd_desc.c
  * @author  MCD Application Team
  * @brief   This file provides the USBD descriptors and string formatting method.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                      www.st.com/SLA0044
  *
  ******************************************************************************
  */
#ifdef USBCON
/* Includes ------------------------------------------------------------------*/
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_ep_conf.h"
#include "usbd_msos20.h"
#include "utils.h"
#include <variant.h>

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* USB VID and PID have to be specified to correct values.
 * They can be defined thanks:
 *   - boards.txt: *.build.vid or *.build.pid
 *   - build_opt.h: define CUSTOM_USBD_VID or CUSTOM_USBD_PID
 * Else if not defined or specified, default to the ST VID,
 * with PID assigned to HID or CDC devices.
 */
#if !defined(USBD_VID) || USBD_VID == 0
  // Undef the default definition
  #undef USBD_VID
  #if defined(CUSTOM_USBD_VID)
    #define USBD_VID    CUSTOM_USBD_VID
  #else
    // Define default values
    #define USBD_VID    0x0483
  #endif
#endif /* USBD_VID */

#if !defined(USBD_PID) || USBD_PID == -1
  // Undef the default definition
  #undef USBD_PID
  #if defined(CUSTOM_USBD_PID)
    #define USBD_PID    CUSTOM_USBD_PID
  #else
    // Define default values, based on the USB class used
    #if defined(USBD_USE_HID_COMPOSITE)
      #define USBD_PID  0x5711
    #elif defined(USBD_USE_CDC)
      #define USBD_PID  0x5740
    #else
      #error "USB PID not specified"
    #endif
  #endif
#endif /* USBD_VID */

#if USBD_VID == 0
  #error "USB VID not properly specified"
#endif

/* Manufacturer string: Use the specified string if specified, guess
   based on VID otherwise */
#if defined(USB_MANUFACTURER_STRING)
  #define USBD_MANUFACTURER_STRING USB_MANUFACTURER_STRING
#elif USBD_VID == 0x2341
  #define USBD_MANUFACTURER_STRING "Arduino LLC"
#elif USBD_VID == 0x2A03
  #define USBD_MANUFACTURER_STRING "Arduino srl"
#elif USBD_VID == 0x0483
  #define USBD_MANUFACTURER_STRING "STMicroelectronics"
#else
  #define USBD_MANUFACTURER_STRING "Unknown"
#endif

#define USBD_LANGID_STRING            0x409   /* 1033 US.S English */

#if defined(TITAN_UNIFIED_STM32)
/* lib/OPTIONS/options.cpp */
extern const char *titan_ProductName(void);
#endif

/* Product string: Use the specified string if specified, construct
   based on BOARD_NAME and class otherwise. */
#if defined(USB_PRODUCT_STRING)
  #define USBD_CLASS_PRODUCT_HS_STRING        USB_PRODUCT_STRING
  #define USBD_CLASS_PRODUCT_FS_STRING        USB_PRODUCT_STRING
#elif defined(USBD_USE_HID_COMPOSITE)
  #define USBD_CLASS_PRODUCT_HS_STRING        CONCATS(BOARD_NAME, "HID in HS Mode")
  #define USBD_CLASS_PRODUCT_FS_STRING        CONCATS(BOARD_NAME, "HID in FS Mode")
#elif defined(USBD_USE_CDC)
  #define USBD_CLASS_PRODUCT_HS_STRING        CONCATS(BOARD_NAME, "CDC in HS Mode")
  #define USBD_CLASS_PRODUCT_FS_STRING        CONCATS(BOARD_NAME, "CDC in FS Mode")
#else
  #define USBD_CLASS_PRODUCT_HS_STRING        CONCATS(BOARD_NAME, "in HS Mode")
  #define USBD_CLASS_PRODUCT_FS_STRING        CONCATS(BOARD_NAME, "in FS Mode")
#endif

#ifdef USBD_USE_HID_COMPOSITE
  #define USBD_CLASS_CONFIGURATION_HS_STRING  CONCATS(BOARD_NAME, "HID Config")
  #define USBD_CLASS_INTERFACE_HS_STRING      CONCATS(BOARD_NAME, "HID Interface")
  #define USBD_CLASS_CONFIGURATION_FS_STRING  CONCATS(BOARD_NAME, "HID Config")
  #define USBD_CLASS_INTERFACE_FS_STRING      CONCATS(BOARD_NAME, "HID Interface")
#endif /* USBD_USE_HID_COMPOSITE */

#ifdef USBD_USE_CDC
  #define USBD_CLASS_CONFIGURATION_HS_STRING  CONCATS(BOARD_NAME, "CDC Config")
  #define USBD_CLASS_INTERFACE_HS_STRING      CONCATS(BOARD_NAME, "CDC Interface")
  #define USBD_CLASS_CONFIGURATION_FS_STRING  CONCATS(BOARD_NAME, "CDC Config")
  #define USBD_CLASS_INTERFACE_FS_STRING      CONCATS(BOARD_NAME, "CDC Interface")
#endif /* USBD_USE_CDC */

/* Per-function interface names. Windows Device Manager shows these; macOS and
 * Chrome do not, so they are documentation more than UI. */
#ifndef USBD_CDC_INTERFACE_STRING
  #define USBD_CDC_INTERFACE_STRING           "TitanLRS Serial"
#endif
#ifndef USBD_VCFG_INTERFACE_STRING
  #define USBD_VCFG_INTERFACE_STRING          "TitanLRS Config"
#endif

/* Private macro -------------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Common function */
uint8_t *USBD_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

/* Class specific */
uint8_t *USBD_Class_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_Class_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_Class_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_Class_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

#if (USBD_CLASS_USER_STRING_DESC == 1)
  uint8_t *USBD_Class_UserStrDescriptor(USBD_SpeedTypeDef speed, uint8_t idx, uint16_t *length);
#endif /* USB_CLASS_USER_STRING_DESC */
#if ((USBD_LPM_ENABLED == 1) || (USBD_CLASS_BOS_ENABLED == 1))
  uint8_t *USBD_USR_BOSDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
#endif /* (USBD_LPM_ENABLED == 1) || (USBD_CLASS_BOS_ENABLED == 1) */
/* Private variables ---------------------------------------------------------*/
USBD_DescriptorsTypeDef USBD_Desc = {
  USBD_Class_DeviceDescriptor,
  USBD_LangIDStrDescriptor,
  USBD_ManufacturerStrDescriptor,
  USBD_Class_ProductStrDescriptor,
  USBD_SerialStrDescriptor,
  USBD_Class_ConfigStrDescriptor,
  USBD_Class_InterfaceStrDescriptor,
#if (USBD_CLASS_USER_STRING_DESC == 1)
  USBD_Class_UserStrDescriptor,
#endif /* USB_CLASS_USER_STRING_DESC */

#if ((USBD_LPM_ENABLED == 1) || (USBD_CLASS_BOS_ENABLED == 1))
  USBD_USR_BOSDescriptor,
#endif /* (USBD_LPM_ENABLED == 1) || (USBD_CLASS_BOS_ENABLED == 1) */
};

#ifdef USBD_USE_HID_COMPOSITE
/* USB Standard Device Descriptor */
__ALIGN_BEGIN uint8_t USBD_Class_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
  0x12,                       /* bLength */
  USB_DESC_TYPE_DEVICE,       /* bDescriptorType */
#if ((USBD_LPM_ENABLED == 1) || (USBD_CLASS_BOS_ENABLED == 1))
  0x01,                       /*bcdUSB */     /* changed to USB version 2.01
                                              in order to support BOS Desc */
#else
  0x00,                       /* bcdUSB */
#endif /* (USBD_LPM_ENABLED == 1) || (USBD_CLASS_BOS_ENABLED == 1) */
  0x02,
  0x00,                       /* bDeviceClass */
  0x00,                       /* bDeviceSubClass */
  0x00,                       /* bDeviceProtocol */
  USB_MAX_EP0_SIZE,           /* bMaxPacketSize */
  LOBYTE(USBD_VID),           /* idVendor */
  HIBYTE(USBD_VID),           /* idVendor */
  LOBYTE(USBD_PID),           /* idProduct */
  HIBYTE(USBD_PID),           /* idProduct */
  0x00,                       /* bcdDevice rel. 2.00 */
  0x02,
  USBD_IDX_MFC_STR,           /* Index of manufacturer string */
  USBD_IDX_PRODUCT_STR,       /* Index of product string */
  USBD_IDX_SERIAL_STR,        /* Index of serial number string */
  USBD_MAX_NUM_CONFIGURATION  /* bNumConfigurations */
}; /* USB_DeviceDescriptor */
#endif /* USBD_USE_HID_COMPOSITE */

#ifdef USBD_USE_CDC
/* USB Standard Device Descriptor */
__ALIGN_BEGIN uint8_t USBD_Class_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
  0x12,                       /* bLength */
  USB_DESC_TYPE_DEVICE,       /* bDescriptorType */
#if ((USBD_LPM_ENABLED == 1) || (USBD_CLASS_BOS_ENABLED == 1))
  0x01,                       /*bcdUSB */     /* changed to USB version 2.01
                                              in order to support BOS Desc */
#else
  0x00,                       /* bcdUSB */
#endif
  0x02,
  /* Miscellaneous / Common Class / Interface Association Descriptor. Required so
   * hosts bind the CDC function and the vendor function separately rather than
   * assuming the whole device is one communications class instance. */
  0xEF,                       /* bDeviceClass: Miscellaneous */
  0x02,                       /* bDeviceSubClass: Common Class */
  0x01,                       /* bDeviceProtocol: Interface Association Descriptor */
  USB_MAX_EP0_SIZE,           /* bMaxPacketSize */
  LOBYTE(USBD_VID),           /* idVendor */
  HIBYTE(USBD_VID),           /* idVendor */
  LOBYTE(USBD_PID),           /* idProduct */
  HIBYTE(USBD_PID),           /* idProduct */
  /* bcdDevice rel. 3.00.
   *
   * Bumped from 2.00 when this became a CDC + vendor composite. Windows caches
   * whether a device supports Microsoft OS descriptors, keyed on
   * VID + PID + bcdDevice (HKLM\SYSTEM\CurrentControlSet\Control\usbflags),
   * and it caches *negative* results too. A device that once enumerated at this
   * VID:PID without MS OS 2.0 descriptors will therefore never be asked for
   * them again, WinUSB never binds to the vendor interface, and WebUSB's
   * open() fails with "Access denied" — with nothing wrong in the descriptors
   * themselves.
   *
   * Bump this again on any future change to usbd_msos20.c, or Windows machines
   * that have already seen the old version will keep the stale answer. */
  0x00,
  0x03,
  USBD_IDX_MFC_STR,           /* Index of manufacturer string */
  USBD_IDX_PRODUCT_STR,       /* Index of product string */
  USBD_IDX_SERIAL_STR,        /* Index of serial number string */
  USBD_MAX_NUM_CONFIGURATION  /* bNumConfigurations */
}; /* USB_DeviceDescriptor */
#endif /* USBD_USE_CDC */

/* USB Device LPM BOS descriptor */
#if (USBD_LPM_ENABLED == 1)
__ALIGN_BEGIN  uint8_t USBD_BOSDesc[USB_SIZ_BOS_DESC] __ALIGN_END = {
  0x5,
  USB_DESC_TYPE_BOS,
  0xC,
  0x0,
  0x1,  /* 1 device capability */
  /* device capability */
  0x7,
  USB_DEVICE_CAPABITY_TYPE,
  0x2,
  0x6, /*LPM capability bit set */
  0x0,
  0x0,
  0x0
};
#endif /* USBD_LPM_ENABLED */

/* USB Device Billboard BOS descriptor Template */
/* The BillBoard BOS template that shipped here is replaced by the BOS in
 * usbd_msos20.c, which carries the Microsoft OS 2.0 platform capability that
 * gets WinUSB bound to the vendor interface. */


/* USB Standard Device Descriptor */
__ALIGN_BEGIN uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
  USB_LEN_LANGID_STR_DESC,
  USB_DESC_TYPE_STRING,
  LOBYTE(USBD_LANGID_STRING),
  HIBYTE(USBD_LANGID_STRING),
};

uint8_t USBD_StringSerial[USB_SIZ_STRING_SERIAL] = {
  USB_SIZ_STRING_SERIAL,
  USB_DESC_TYPE_STRING,
};

__ALIGN_BEGIN uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;

/* Private functions ---------------------------------------------------------*/
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len);
static void Get_SerialNum(void);

/**
  * @brief  Returns the device descriptor.
  * @param  speed: Current device speed
  * @param  length: Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t *USBD_Class_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_Class_DeviceDesc);
  return (uint8_t *)USBD_Class_DeviceDesc;
}

/**
  * @brief  Returns the LangID string descriptor.
  * @param  speed: Current device speed
  * @param  length: Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t *USBD_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_LangIDDesc);
  return (uint8_t *)USBD_LangIDDesc;
}

/**
  * @brief  Returns the product string descriptor.
  * @param  speed: Current device speed
  * @param  length: Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t *USBD_Class_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
#if defined(TITAN_UNIFIED_STM32)
  /* The unified targets name themselves from the product_name the web flasher patched into the
   * firmware slot. It is read straight from flash, so it is valid at enumeration time even
   * though USB comes up in premain(), before setup(). */
  UNUSED(speed);
  USBD_GetString((uint8_t *)titan_ProductName(), USBD_StrDesc, length);
#else
  if (speed == USBD_SPEED_HIGH) {
    USBD_GetString((uint8_t *)USBD_CLASS_PRODUCT_HS_STRING, USBD_StrDesc, length);
  } else {
    USBD_GetString((uint8_t *)USBD_CLASS_PRODUCT_FS_STRING, USBD_StrDesc, length);
  }
#endif
  return USBD_StrDesc;
}

/**
  * @brief  Returns the manufacturer string descriptor.
  * @param  speed: Current device speed
  * @param  length: Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t *USBD_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

/**
  * @brief  Returns the serial number string descriptor.
  * @param  speed: Current device speed
  * @param  length: Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t *USBD_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);

  *length = USB_SIZ_STRING_SERIAL;

  /* Update the serial number string descriptor with the data from the unique ID*/
  Get_SerialNum();

  return (uint8_t *)USBD_StringSerial;
}

/**
  * @brief  Returns the configuration string descriptor.
  * @param  speed: Current device speed
  * @param  length: Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t *USBD_Class_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if (speed == USBD_SPEED_HIGH) {
    USBD_GetString((uint8_t *)USBD_CLASS_CONFIGURATION_HS_STRING, USBD_StrDesc, length);
  } else {
    USBD_GetString((uint8_t *)USBD_CLASS_CONFIGURATION_FS_STRING, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Returns the interface string descriptor.
  * @param  speed: Current device speed
  * @param  length: Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t *USBD_Class_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if (speed == USBD_SPEED_HIGH) {
    USBD_GetString((uint8_t *)USBD_CLASS_INTERFACE_HS_STRING, USBD_StrDesc, length);
  } else {
    USBD_GetString((uint8_t *)USBD_CLASS_INTERFACE_FS_STRING, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Create the serial number string descriptor
  * @param  None
  * @retval None
  */
static void Get_SerialNum(void)
{
  uint32_t deviceserial0;
  uint32_t deviceserial1;
  uint32_t deviceserial2;

  deviceserial0 = *(uint32_t *)DEVICE_ID1;
  deviceserial1 = *(uint32_t *)DEVICE_ID2;
  deviceserial2 = *(uint32_t *)DEVICE_ID3;

  deviceserial0 += deviceserial2;

  if (deviceserial0 != 0U) {
    IntToUnicode(deviceserial0, &USBD_StringSerial[2], 8U);
    IntToUnicode(deviceserial1, &USBD_StringSerial[18], 4U);
  }
}


#if ((USBD_LPM_ENABLED == 1) || (USBD_CLASS_BOS_ENABLED == 1))
/**
  * @brief  USBD_USR_BOSDescriptor
  *         return the BOS descriptor
  * @param  speed : current device speed
  * @param  length : pointer to data length variable
  * @retval pointer to descriptor buffer
  */
uint8_t *USBD_USR_BOSDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = USBD_BOSDescriptor_len;
  return (uint8_t *)USBD_BOSDescriptor;
}
#endif /* (USBD_LPM_ENABLED == 1) || (USBD_CLASS_BOS_ENABLED == 1) */


#if (USBD_CLASS_USER_STRING_DESC == 1)
/**
  * @brief  Returns the Class User string descriptor.
  * @param  speed: Current device speed
  * @param  idx: index of string descriptor
  * @param  length: Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t *USBD_Class_UserStrDescriptor(USBD_SpeedTypeDef speed, uint8_t idx, uint16_t *length)
{
  UNUSED(speed);

  switch (idx) {
    case USBD_IDX_CDC_STR:
      USBD_GetString((uint8_t *)USBD_CDC_INTERFACE_STRING, USBD_StrDesc, length);
      break;
    case USBD_IDX_VCFG_STR:
      USBD_GetString((uint8_t *)USBD_VCFG_INTERFACE_STRING, USBD_StrDesc, length);
      break;
    default:
      /* Unknown index: a well-formed empty string, rather than the
         uninitialised buffer (and unset length) the stock version returned. */
      USBD_StrDesc[0] = 2U;
      USBD_StrDesc[1] = USB_DESC_TYPE_STRING;
      *length = 2U;
      break;
  }

  return USBD_StrDesc;
}
#endif /* USBD_CLASS_USER_STRING_DESC */


/**
  * @brief  Convert Hex 32Bits value into char
  * @param  value: value to convert
  * @param  pbuf: pointer to the buffer
  * @param  len: buffer length
  * @retval None
  */
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
  uint8_t idx = 0U;

  for (idx = 0U ; idx < len ; idx ++) {
    if (((value >> 28)) < 0xAU) {
      pbuf[ 2U * idx] = (value >> 28) + '0';
    } else {
      pbuf[2U * idx] = (value >> 28) + 'A' - 10U;
    }

    value = value << 4;

    pbuf[2U * idx + 1] = 0U;
  }
}
#endif /* USBCON */
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
