/**
  ******************************************************************************
  * @file    usbd_ep_conf.c
  * @brief   FIFO sizing for the TitanLRS composite device.
  *
  * ep_def[0] is the shared receive FIFO; every other entry is an IN endpoint and
  * gets a dedicated transmit FIFO, numbered by its endpoint number — so the IN
  * endpoints have to be listed in order with no gaps. OUT endpoints are deliberately absent — they
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
  /* The receive FIFO is shared by every OUT endpoint, and NCM streams a whole
   * transfer block (up to 2 KB) through it packet by packet, so it gets the
   * largest share. EP0 only ever sends 64-byte packets. */
  #define FIFO_RX_SIZE      128U
  #define FIFO_EP0_SIZE      32U
  #define FIFO_DATA_SIZE     64U
  #define FIFO_TOTAL_WORDS  320U
#endif
#define FIFO_CMD_SIZE         8U
#define FIFO_NOTIF_SIZE      16U

/* Each IN endpoint's transmit FIFO is numbered by its endpoint number. */
const ep_desc_t ep_def[] = {
  {0x00U,       FIFO_RX_SIZE},    /* shared receive FIFO (all OUT endpoints) */
  {0x80U,       FIFO_EP0_SIZE},   /* EP0 IN */
#if USB_HAS_CDC_ACM
  {CDC_IN_EP,   FIFO_DATA_SIZE},  /* CDC data IN */
  {CDC_CMD_EP,  FIFO_CMD_SIZE},   /* CDC notification */
#endif
  {NCM_IN_EP,   FIFO_DATA_SIZE},  /* NCM data IN */
  {NCM_NOTIF_EP, FIFO_NOTIF_SIZE}, /* NCM notification */
};

_Static_assert(FIFO_RX_SIZE + FIFO_EP0_SIZE + FIFO_DATA_SIZE + FIFO_NOTIF_SIZE
               + (USB_HAS_CDC_ACM ? FIFO_DATA_SIZE + FIFO_CMD_SIZE : 0U) <= FIFO_TOTAL_WORDS,
               "USB composite FIFO allocation exceeds the OTG core's dedicated FIFO RAM");

const uint32_t ep_def_count = sizeof(ep_def) / sizeof(ep_def[0]);

#if defined(USB_OTG_FS_MAX_IN_ENDPOINTS) && !defined(USE_USB_HS)
/* Every entry but the RX FIFO is an IN endpoint, EP0 included, as in the CMSIS count. */
_Static_assert(sizeof(ep_def) / sizeof(ep_def[0]) - 1U <= USB_OTG_FS_MAX_IN_ENDPOINTS,
               "USB composite uses more IN endpoints than the OTG_FS core has");
#endif

#endif /* HAL_PCD_MODULE_ENABLED && USBCON */
