#pragma once

/*
 * STM32F4: USB and network limits. 128 KB of RAM in all, RX only: the largest HTTP bodies are the
 * slot's layout and options (2 KB, 512 bytes).
 */

/* OTG_FS has 4 IN endpoints including EP0, which is room for one CDC function: NCM only. */
#define USB_HAS_CDC_ACM         0

/* One main SRAM, so the NCM transfer blocks and the lwIP heap and pools are ordinary .bss. */
#define USBNET_BUF_SECTION

/* lwipopts.h */
#define USBNET_MEM_SIZE         (24 * 1024)
#define USBNET_TCP_SEG_COUNT    12
#define USBNET_PBUF_POOL_SIZE   6
#define USBNET_TCP_WND_MSS      2
#define USBNET_TCP_SND_BUF_MSS  2

/* http_server */
#define HTTP_MAX_CONNS          3
#define HTTP_MAX_BODY           8192
