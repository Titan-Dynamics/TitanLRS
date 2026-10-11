/**
  ******************************************************************************
  * @file    usbd_composite.h
  * @brief   CDC-ACM + CDC-NCM composite USB device class for TitanLRS.
  *
  * One USBD class driver presenting two functions:
  *
  *   CDC-ACM (interfaces 0+1) — `Serial`, unchanged from the stock stm32duino
  *   device. MAVLink, CRSF and the debug log. One COM port, as before.
  *
  *   CDC-NCM (interfaces 2+3) — a USB network adapter (usbd_ncm.c). lib/USBNet
  *   runs lwIP on it: the HTTP config API and MAVLink over UDP. The OS binds
  *   its own network driver, so it never shows up as a port.
  *
  * Why one class driver rather than two registered classes: the stm32duino
  * build of the ST device library is compiled without USE_USBD_COMPOSITE, so
  * USBD_MAX_SUPPORTED_CLASS is 1 and USBD_CoreFindIF()/USBD_CoreFindEP() return
  * 0 unconditionally — every event is routed to pClass[0]. This driver owns
  * both functions and demultiplexes internally on interface number (control)
  * and endpoint number (data), which also avoids ST's CompositeBuilder.
  *
  * The CDC half is derived from ST's usbd_cdc.c.
  ******************************************************************************
  */

#ifndef __USBD_COMPOSITE_H
#define __USBD_COMPOSITE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ioreq.h"
#include "usbd_ep_conf.h"

#ifndef CDC_HS_BINTERVAL
#define CDC_HS_BINTERVAL                            0x10U
#endif
#ifndef CDC_FS_BINTERVAL
#define CDC_FS_BINTERVAL                            0x10U
#endif

#define CDC_REQ_MAX_DATA_SIZE                       0x7U

/* CDC class requests */
#define CDC_SEND_ENCAPSULATED_COMMAND               0x00U
#define CDC_GET_ENCAPSULATED_RESPONSE               0x01U
#define CDC_SET_COMM_FEATURE                        0x02U
#define CDC_GET_COMM_FEATURE                        0x03U
#define CDC_CLEAR_COMM_FEATURE                      0x04U
#define CDC_SET_LINE_CODING                         0x20U
#define CDC_GET_LINE_CODING                         0x21U
#define CDC_SET_CONTROL_LINE_STATE                  0x22U
#define CDC_SEND_BREAK                              0x23U

/* Control Line State bits */
#define CLS_DTR                                     (1 << 0)
#define CLS_RTS                                     (1 << 1)

typedef struct {
  uint32_t bitrate;
  uint8_t  format;
  uint8_t  paritytype;
  uint8_t  datatype;
} USBD_CDC_LineCodingTypeDef;

typedef struct _USBD_CDC_Itf {
  int8_t (*Init)(void);
  int8_t (*DeInit)(void);
  int8_t (*Control)(uint8_t cmd, uint8_t *pbuf, uint16_t length);
  int8_t (*Receive)(uint8_t *Buf, uint32_t *Len);
  int8_t (*TransmitCplt)(uint8_t *Buf, uint32_t *Len, uint8_t epnum);
} USBD_CDC_ItfTypeDef;

typedef struct {
  uint32_t data[CDC_DATA_MAX_PACKET_SIZE / 4U]; /* forces 32-bit alignment */
  uint8_t  CmdOpCode;
  uint8_t  CmdLength;
  uint8_t  *RxBuffer;
  uint8_t  *TxBuffer;
  uint32_t RxLength;
  uint32_t TxLength;
  __IO uint32_t TxState;
  __IO uint32_t RxState;
} USBD_CDC_HandleTypeDef;

extern USBD_ClassTypeDef USBD_Composite;
#define USBD_COMPOSITE_CLASS &USBD_Composite

uint8_t USBD_Composite_RegisterCDC(USBD_HandleTypeDef *pdev, USBD_CDC_ItfTypeDef *fops);

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff, uint32_t length);
uint8_t USBD_CDC_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff);
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef *pdev);
uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef *pdev);
uint8_t USBD_CDC_ClearBuffer(USBD_HandleTypeDef *pdev);


USBD_CDC_HandleTypeDef *USBD_CDC_Handle(void);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_COMPOSITE_H */
