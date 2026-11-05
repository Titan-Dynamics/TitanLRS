#pragma once

/*
 * STM32H7: USB and network limits. HTTP bodies reach ~24 KB for a TX models import/export.
 */

/* OTG has enough IN endpoints for both functions: CDC-ACM (Serial) and CDC-NCM. */
#define USB_HAS_CDC_ACM         1

/* AXI SRAM, because DTCM holds .bss, the heap and the stack and is the scarce one. The section is
 * NOLOAD (variants/ldscript_gen.ld): lwIP and the NCM driver initialise what they use. */
#define USBNET_BUF_SECTION      __attribute__((section(".axisram")))

/* lwipopts.h */
#define USBNET_MEM_SIZE         (96 * 1024)
#define USBNET_TCP_SEG_COUNT    24
#define USBNET_PBUF_POOL_SIZE   12
#define USBNET_TCP_WND_MSS      4
#define USBNET_TCP_SND_BUF_MSS  4

/* http_server */
#define HTTP_MAX_CONNS          4
#define HTTP_MAX_BODY           24576
