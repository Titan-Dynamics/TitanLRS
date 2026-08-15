/**
  ******************************************************************************
  * @file    usbd_ep_conf.c
  * @brief   FIFO sizing for the TitanLRS composite device.
  *
  * ep_def[0] is the shared receive FIFO; every other entry is an IN endpoint and
  * gets a dedicated transmit FIFO. OUT endpoints are deliberately absent — they
  * all draw from the single shared RX FIFO, so listing them (as the stm32duino
  * original did) only wasted FIFO words.
  ******************************************************************************
  */
#if defined(HAL_PCD_MODULE_ENABLED) && defined(USBCON)

#include "usbd_ep_conf.h"

/* FIFO sizes in 32-bit words. The OTG core has a fixed pool of dedicated FIFO
 * RAM shared by the RX FIFO and every TX FIFO: 1.25 KB (320 words) on OTG_FS,
 * 4 KB (1024 words) on OTG_HS. */
#ifdef USE_USB_HS
  #define FIFO_RX_SIZE      256U
  #define FIFO_EP0_SIZE      64U
  #define FIFO_DATA_SIZE    256U
  #define FIFO_TOTAL_WORDS 1024U
#else
  #define FIFO_RX_SIZE       64U
  #define FIFO_EP0_SIZE      64U
  #define FIFO_DATA_SIZE     64U
  #define FIFO_TOTAL_WORDS  320U
#endif
#define FIFO_CMD_SIZE         8U

const ep_desc_t ep_def[] = {
  {0x00U,       FIFO_RX_SIZE},    /* shared receive FIFO (all OUT endpoints) */
  {0x80U,       FIFO_EP0_SIZE},   /* EP0 IN            -> TX FIFO 0 */
  {CDC_IN_EP,   FIFO_DATA_SIZE},  /* CDC data IN       -> TX FIFO 1 */
  {CDC_CMD_EP,  FIFO_CMD_SIZE},   /* CDC notification  -> TX FIFO 2 */
  {VCFG_IN_EP,  FIFO_DATA_SIZE},  /* vendor bulk IN    -> TX FIFO 3 */
};

const uint32_t ep_def_count = sizeof(ep_def) / sizeof(ep_def[0]);

_Static_assert(FIFO_RX_SIZE + FIFO_EP0_SIZE + 2U * FIFO_DATA_SIZE + FIFO_CMD_SIZE
               <= FIFO_TOTAL_WORDS,
               "USB composite FIFO allocation exceeds the OTG core's dedicated FIFO RAM");

#endif /* HAL_PCD_MODULE_ENABLED && USBCON */
