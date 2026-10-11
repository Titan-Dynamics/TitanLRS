/**
  ******************************************************************************
  * @file    usbd_ncm.h
  * @brief   CDC-NCM (USB network adapter) function of the TitanLRS composite device.
  *
  * The host sees an Ethernet adapter; lib/USBNet runs lwIP on the device end. Frames travel in
  * NCM Transfer Blocks (NTB16): a header, the datagrams, and a datagram pointer table.
  *
  * Context split — the radio timing depends on it:
  *   - The USB interrupt (priority 1, above the radio) only moves 64-byte packets in and out of
  *     the transfer-block buffers and sets flags. No parsing, no lwIP.
  *   - Everything else — splitting a received block into frames, packing frames into a block,
  *     starting transmissions — runs from the main loop through the NCM_* functions below.
  *
  * Receive is done one packet at a time, with the block length taken from the NTB header, rather
  * than as one large transfer that ends on a short packet: the Windows 10 NCM driver does not send
  * the zero-length packet that should end a block whose size is a multiple of 64 bytes, and a
  * single large transfer would then never complete.
  *
  * Windows binds its inbox UsbNcm.sys through the WINNCM compatible ID (usbd_msos20.c); macOS and
  * Linux bind NCM natively.
  ******************************************************************************
  */

#ifndef __USBD_NCM_H
#define __USBD_NCM_H

#ifdef USBCON

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "usbd_ioreq.h"
#include "usbd_ep_conf.h"

/* Largest transfer block in either direction, and therefore the buffer size. A full-size
 * Ethernet frame plus the NTB16 header and pointer table fits with room for a second small one. */
#define NCM_NTB_MAX_SIZE        2048U

/* Ethernet MTU-sized frame + header, the largest datagram the host may send us. */
#define NCM_MAX_SEGMENT_SIZE    1514U

/* --- class driver hooks, called by usbd_composite.c (interrupt context) ------------------- */

void    NCM_ClassInit(USBD_HandleTypeDef *pdev);
void    NCM_ClassDeInit(USBD_HandleTypeDef *pdev);
/* Class requests addressed to the NCM communication interface. */
uint8_t NCM_ClassSetup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
/* EP0 OUT data stage of an NCM class request; returns true if it was ours. */
bool    NCM_EP0_RxReady(USBD_HandleTypeDef *pdev);
/* SET_INTERFACE / GET_INTERFACE for the NCM data interface. */
uint8_t NCM_SetAltSetting(USBD_HandleTypeDef *pdev, uint8_t alt);
uint8_t NCM_GetAltSetting(void);
void    NCM_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
void    NCM_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);

/* --- main-loop API (lib/USBNet) ----------------------------------------------------------- */

/* Sends the link notifications once they are due. Call every main-loop iteration. */
void NCM_Service(void);

/* True while the host has the data interface selected (alternate setting 1). */
bool NCM_LinkUp(void);

/* Hands every frame received since the last call to `deliver`, then re-arms reception. */
void NCM_Receive(void (*deliver)(const uint8_t *frame, uint16_t len, void *ctx), void *ctx);

/* Reserves room for one outgoing frame of `len` bytes in the block being assembled and returns
 * where to write it, or NULL if the link is down or there is no room yet (the frame is dropped,
 * as a busy Ethernet MAC would). Call NCM_TxCommit() once the frame is written. */
uint8_t *NCM_TxReserve(uint16_t len);
void     NCM_TxCommit(void);

/* Starts sending the assembled block if the IN endpoint is free. Cheap; call every loop. */
void NCM_TxKick(void);

/* The two Ethernet addresses: the host's end (reported in the iMACAddress string) and ours.
 * Both are locally administered and derived from the chip UID. */
void NCM_HostMac(uint8_t mac[6]);
void NCM_DeviceMac(uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* USBCON */
#endif /* __USBD_NCM_H */
