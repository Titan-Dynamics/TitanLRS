/**
  ******************************************************************************
  * @file    usbd_composite_if.c
  * @brief   Device bring-up for the TitanLRS composite device.
  *
  * Derived from the stm32duino usbd_cdc_if.c.
  * Copyright (c) 2015 STMicroelectronics.
  ******************************************************************************
  */

#ifdef USBCON

#include "usbd_desc.h"
#include "usbd_composite_if.h"

/* USB Device Core handle (one device, two functions) */
USBD_HandleTypeDef hUSBD_Device_CDC;

static bool USBComposite_initialized = false;
#if defined(ICACHE) && defined (HAL_ICACHE_MODULE_ENABLED) && !defined(HAL_ICACHE_MODULE_DISABLED)
  static bool icache_enabled = false;
#endif

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
    /* Serial can be used from here on, before any host has enumerated the device (and always, on
     * a family without the CDC-ACM function). */
    CDC_TransmitQueue_Init(&TransmitQueue);
    CDC_ReceiveQueue_Init(&ReceiveQueue);
    if (USBD_Init(&hUSBD_Device_CDC, &USBD_Desc, 0) == USBD_OK) {
      if (USBD_RegisterClass(&hUSBD_Device_CDC, USBD_COMPOSITE_CLASS) == USBD_OK) {
        USBD_Start(&hUSBD_Device_CDC);
        USBComposite_initialized = true;
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

#endif /* USBCON */
