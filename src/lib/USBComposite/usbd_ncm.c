/**
  ******************************************************************************
  * @file    usbd_ncm.c
  * @brief   CDC-NCM function of the TitanLRS composite device. See usbd_ncm.h.
  *
  * Transfer-block layout follows the USB CDC NCM 1.0 specification (NTB16 only). The approach to
  * batching and to the notification sequence follows TinyUSB's ncm_device.c (MIT licence).
  ******************************************************************************
  */

#ifdef USBCON

#include "usbd_ncm.h"
#include "usbd_desc.h"
#include "usbd_composite_if.h"

/* NCM class requests (CDC NCM 1.0 table 6-2, plus the CDC ECM packet filter) */
#define NCM_SET_ETHERNET_PACKET_FILTER  0x43U
#define NCM_GET_NTB_PARAMETERS          0x80U
#define NCM_GET_NTB_FORMAT              0x83U
#define NCM_SET_NTB_FORMAT              0x84U
#define NCM_GET_NTB_INPUT_SIZE          0x85U
#define NCM_SET_NTB_INPUT_SIZE          0x86U

/* Notification codes */
#define NCM_NOTIFY_NETWORK_CONNECTION   0x00U
#define NCM_NOTIFY_SPEED_CHANGE         0x2AU

#define NTH16_SIGNATURE                 0x484D434EUL   /* "NCMH" */
#define NDP16_SIGNATURE_NCM0            0x304D434EUL   /* "NCM0": no CRC */
#define NTH16_LEN                       12U
#define NDP16_HEADER_LEN                8U

/* Datagram alignment we ask the host to use, and use ourselves */
#define NCM_ALIGN                       4U
/* Most datagrams we pack into one outgoing block */
#define NCM_TX_MAX_DATAGRAMS            8U
/* Link speed reported to the host: full speed USB */
#define NCM_LINK_BPS                    12000000UL
/* How long after SET_INTERFACE(alt 1) the link notifications start. They must not go out while
 * that control transfer is still in progress: macOS's NCM driver, when the speed notification
 * reaches it before the request's status stage, switches the data interface straight back to
 * alternate setting 0 and never retries, leaving the adapter "Not connected". */
#define NCM_NOTIFY_DELAY_MS             5U
/* Notification endpoint polling interval, in frames (ms) */
#define NCM_NOTIF_BINTERVAL             0x10U
/* Longest the main loop waits for the in-flight block before dropping a frame. A full 2 KB block
 * takes under 2 ms at full speed, so this only expires if the host has stopped reading. */
#define NCM_TX_WAIT_MS                  5U

#define ALIGN_UP(v, a)  (((v) + ((a) - 1U)) & ~((a) - 1U))

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* --- state ------------------------------------------------------------------------------- */

static uint8_t s_alt = 0U;
static volatile bool s_linkUp = false;

/* Transfer-block buffers. OTG DMA is off (the CPU moves FIFO data), so AXI SRAM is fine. */
__attribute__((aligned(4), section(".axisram"))) static uint8_t s_rxBuf[2][NCM_NTB_MAX_SIZE];
__attribute__((aligned(4), section(".axisram"))) static uint8_t s_txBuf[2][NCM_NTB_MAX_SIZE];

/* Receive: the interrupt fills s_rxBuf[s_rxCur]; a non-zero s_rxLen[i] is a complete block
 * waiting for the main loop. s_rxArmed is false while both buffers are full. */
static volatile uint8_t  s_rxCur = 0U;
static volatile uint16_t s_rxLen[2] = {0U, 0U};
static volatile bool     s_rxArmed = false;
static uint16_t s_rxOff = 0U;
static uint16_t s_rxBlockLen = 0U;

/* Transmit: the main loop assembles a block in s_txBuf[s_txFill] while the other one may be in
 * flight. */
static volatile bool s_txBusy = false;
static uint8_t  s_txFill = 0U;
static uint16_t s_txEnd = NTH16_LEN;
static uint8_t  s_txCount = 0U;
static uint16_t s_txOff[NCM_TX_MAX_DATAGRAMS];
static uint16_t s_txDgLen[NCM_TX_MAX_DATAGRAMS];
static uint16_t s_txPendingOff = 0U;
static uint16_t s_txPendingLen = 0U;
static uint16_t s_txSeq = 0U;
static uint32_t s_ntbInMax = NCM_NTB_MAX_SIZE;

/* Control requests and notifications */
__ALIGN_BEGIN static uint8_t s_ep0Buf[32] __ALIGN_END;
static uint8_t s_ep0Pending = 0U;
__ALIGN_BEGIN static uint8_t s_notif[16] __ALIGN_END;
typedef enum { NOTIFY_IDLE, NOTIFY_SPEED_SENT, NOTIFY_DONE } notify_state_t;
static volatile notify_state_t s_notifyState = NOTIFY_IDLE;
static volatile bool s_notifyDue = false;
static volatile uint32_t s_notifyDueMs = 0U;

/* --- helpers ----------------------------------------------------------------------------- */

static void rxArm(USBD_HandleTypeDef *pdev)
{
  s_rxArmed = true;
  (void)USBD_LL_PrepareReceive(pdev, NCM_OUT_EP, &s_rxBuf[s_rxCur][s_rxOff], NCM_DATA_MAX_PACKET_SIZE);
}

static void resetDataPath(void)
{
  s_rxCur = 0U;
  s_rxLen[0] = 0U;
  s_rxLen[1] = 0U;
  s_rxOff = 0U;
  s_rxBlockLen = 0U;
  s_rxArmed = false;

  s_txBusy = false;
  s_txFill = 0U;
  s_txEnd = NTH16_LEN;
  s_txCount = 0U;
}

static void sendSpeedNotification(USBD_HandleTypeDef *pdev)
{
  s_notif[0] = 0xA1U;                   /* bmRequestType: class, interface, device-to-host */
  s_notif[1] = NCM_NOTIFY_SPEED_CHANGE;
  wr16(&s_notif[2], 0U);
  wr16(&s_notif[4], NCM_COMM_ITF);
  wr16(&s_notif[6], 8U);
  wr32(&s_notif[8], NCM_LINK_BPS);      /* DLBitRate */
  wr32(&s_notif[12], NCM_LINK_BPS);     /* ULBitRate */
  s_notifyState = NOTIFY_SPEED_SENT;
  pdev->ep_in[NCM_NOTIF_EP & 0x0FU].total_length = 16U;
  (void)USBD_LL_Transmit(pdev, NCM_NOTIF_EP, s_notif, 16U);
}

static void sendConnectedNotification(USBD_HandleTypeDef *pdev)
{
  s_notif[0] = 0xA1U;
  s_notif[1] = NCM_NOTIFY_NETWORK_CONNECTION;
  wr16(&s_notif[2], 1U);                /* connected */
  wr16(&s_notif[4], NCM_COMM_ITF);
  wr16(&s_notif[6], 0U);
  s_notifyState = NOTIFY_DONE;
  pdev->ep_in[NCM_NOTIF_EP & 0x0FU].total_length = 8U;
  (void)USBD_LL_Transmit(pdev, NCM_NOTIF_EP, s_notif, 8U);
}

/* --- class driver hooks ------------------------------------------------------------------ */

void NCM_ClassInit(USBD_HandleTypeDef *pdev)
{
  s_alt = 0U;
  s_linkUp = false;
  s_notifyState = NOTIFY_IDLE;
  s_ep0Pending = 0U;
  s_ntbInMax = NCM_NTB_MAX_SIZE;
  resetDataPath();

  /* The notification endpoint belongs to the communication interface, which has no alternate
   * settings: it is open for the whole configuration. The data endpoints only exist in
   * alternate setting 1 of the data interface. */
  (void)USBD_LL_OpenEP(pdev, NCM_NOTIF_EP, USBD_EP_TYPE_INTR, NCM_NOTIF_PACKET_SIZE);
  pdev->ep_in[NCM_NOTIF_EP & 0x0FU].is_used = 1U;
  pdev->ep_in[NCM_NOTIF_EP & 0x0FU].bInterval = NCM_NOTIF_BINTERVAL;
}

void NCM_ClassDeInit(USBD_HandleTypeDef *pdev)
{
  (void)NCM_SetAltSetting(pdev, 0U);
  (void)USBD_LL_CloseEP(pdev, NCM_NOTIF_EP);
  pdev->ep_in[NCM_NOTIF_EP & 0x0FU].is_used = 0U;
  pdev->ep_in[NCM_NOTIF_EP & 0x0FU].bInterval = 0U;
  s_notifyState = NOTIFY_IDLE;
}

uint8_t NCM_ClassSetup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  switch (req->bRequest) {
    case NCM_GET_NTB_PARAMETERS: {
      uint8_t *p = s_ep0Buf;
      wr16(&p[0], 28U);                       /* wLength */
      wr16(&p[2], 0x0001U);                   /* bmNtbFormatsSupported: NTB16 only */
      wr32(&p[4], NCM_NTB_MAX_SIZE);          /* dwNtbInMaxSize */
      wr16(&p[8], NCM_ALIGN);                 /* wNdpInDivisor */
      wr16(&p[10], 0U);                       /* wNdpInPayloadRemainder */
      wr16(&p[12], NCM_ALIGN);                /* wNdpInAlignment */
      wr16(&p[14], 0U);                       /* reserved */
      wr32(&p[16], NCM_NTB_MAX_SIZE);         /* dwNtbOutMaxSize */
      wr16(&p[20], NCM_ALIGN);                /* wNdpOutDivisor */
      wr16(&p[22], 0U);                       /* wNdpOutPayloadRemainder */
      wr16(&p[24], NCM_ALIGN);                /* wNdpOutAlignment */
      wr16(&p[26], 0U);                       /* wNtbOutMaxDatagrams: no limit */
      (void)USBD_CtlSendData(pdev, p, (uint16_t)MIN(28U, req->wLength));
      return (uint8_t)USBD_OK;
    }

    case NCM_GET_NTB_FORMAT:
      wr16(s_ep0Buf, 0U);                     /* NTB16 */
      (void)USBD_CtlSendData(pdev, s_ep0Buf, (uint16_t)MIN(2U, req->wLength));
      return (uint8_t)USBD_OK;

    case NCM_SET_NTB_FORMAT:
      if (req->wValue != 0U) {                /* only NTB16 */
        break;
      }
      return (uint8_t)USBD_OK;

    case NCM_GET_NTB_INPUT_SIZE:
      wr32(s_ep0Buf, s_ntbInMax);
      (void)USBD_CtlSendData(pdev, s_ep0Buf, (uint16_t)MIN(4U, req->wLength));
      return (uint8_t)USBD_OK;

    case NCM_SET_NTB_INPUT_SIZE:
      if ((req->wLength != 4U) && (req->wLength != 8U)) {
        break;
      }
      s_ep0Pending = NCM_SET_NTB_INPUT_SIZE;
      (void)USBD_CtlPrepareRx(pdev, s_ep0Buf, req->wLength);
      return (uint8_t)USBD_OK;

    case NCM_SET_ETHERNET_PACKET_FILTER:
      /* We deliver everything the host sends and send only what lwIP asks for, so there is
       * nothing to filter. */
      return (uint8_t)USBD_OK;

    default:
      break;
  }

  USBD_CtlError(pdev, req);
  return (uint8_t)USBD_FAIL;
}

bool NCM_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);
  if (s_ep0Pending != NCM_SET_NTB_INPUT_SIZE) {
    return false;
  }
  s_ep0Pending = 0U;
  uint32_t size = rd32(s_ep0Buf);
  if (size > NCM_NTB_MAX_SIZE) {
    size = NCM_NTB_MAX_SIZE;
  }
  if (size >= NTH16_LEN + NDP16_HEADER_LEN + 8U + 64U) {
    s_ntbInMax = size;
  }
  return true;
}

uint8_t NCM_SetAltSetting(USBD_HandleTypeDef *pdev, uint8_t alt)
{
  if (alt > 1U) {
    return (uint8_t)USBD_FAIL;
  }

  if (s_alt == 1U) {
    s_linkUp = false;
    (void)USBD_LL_CloseEP(pdev, NCM_OUT_EP);
    pdev->ep_out[NCM_OUT_EP & 0x0FU].is_used = 0U;
    (void)USBD_LL_CloseEP(pdev, NCM_IN_EP);
    pdev->ep_in[NCM_IN_EP & 0x0FU].is_used = 0U;
  }
  resetDataPath();
  s_alt = alt;

  if (alt == 1U) {
    (void)USBD_LL_OpenEP(pdev, NCM_IN_EP, USBD_EP_TYPE_BULK, NCM_DATA_MAX_PACKET_SIZE);
    pdev->ep_in[NCM_IN_EP & 0x0FU].is_used = 1U;
    (void)USBD_LL_OpenEP(pdev, NCM_OUT_EP, USBD_EP_TYPE_BULK, NCM_DATA_MAX_PACKET_SIZE);
    pdev->ep_out[NCM_OUT_EP & 0x0FU].is_used = 1U;
    rxArm(pdev);
    s_linkUp = true;
    /* Hosts wait for the speed + connection notifications before they treat the adapter as
     * connected. Sent from NCM_Service() once this request has completed, never from here. */
    s_notifyDueMs = HAL_GetTick() + NCM_NOTIFY_DELAY_MS;
    s_notifyDue = true;
  } else {
    s_notifyDue = false;
  }
  return (uint8_t)USBD_OK;
}

uint8_t NCM_GetAltSetting(void)
{
  return s_alt;
}

void NCM_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  const uint8_t ep = epnum & 0x0FU;
  if (ep == (NCM_IN_EP & 0x0FU)) {
    s_txBusy = false;
  } else if (ep == (NCM_NOTIF_EP & 0x0FU)) {
    if (s_notifyState == NOTIFY_SPEED_SENT) {
      sendConnectedNotification(pdev);
    }
  }
}

void NCM_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  const uint16_t n = (uint16_t)USBD_LL_GetRxDataSize(pdev, epnum);
  uint8_t *buf = s_rxBuf[s_rxCur];

  if ((s_rxOff == 0U) && (n == 0U)) {
    /* The zero-length packet ending a block we already completed from its header length. */
    rxArm(pdev);
    return;
  }
  s_rxOff = (uint16_t)(s_rxOff + n);

  if ((s_rxBlockLen == 0U) && (s_rxOff >= NTH16_LEN)) {
    const uint16_t blockLen = rd16(&buf[8]);
    if ((rd32(buf) != NTH16_SIGNATURE) || (blockLen < NTH16_LEN) || (blockLen > NCM_NTB_MAX_SIZE)) {
      /* Lost sync: drop it and start again from the next packet. */
      s_rxOff = 0U;
      rxArm(pdev);
      return;
    }
    s_rxBlockLen = blockLen;
  }

  const bool shortPacket = n < NCM_DATA_MAX_PACKET_SIZE;
  const bool haveAll = (s_rxBlockLen != 0U) && (s_rxOff >= s_rxBlockLen);
  const bool full = (uint32_t)s_rxOff + NCM_DATA_MAX_PACKET_SIZE > NCM_NTB_MAX_SIZE;
  if (!shortPacket && !haveAll && !full) {
    rxArm(pdev);
    return;
  }

  const bool complete = haveAll;
  const uint16_t blockLen = s_rxBlockLen;
  s_rxOff = 0U;
  s_rxBlockLen = 0U;
  if (!complete) {
    /* Ended early (or overran the buffer): not a usable block. */
    rxArm(pdev);
    return;
  }

  s_rxLen[s_rxCur] = blockLen;
  const uint8_t next = (uint8_t)(s_rxCur ^ 1U);
  if (s_rxLen[next] == 0U) {
    s_rxCur = next;
    rxArm(pdev);
  } else {
    /* Both buffers are waiting for the main loop; it re-arms once one is free. */
    s_rxArmed = false;
  }
}

/* --- main-loop API ----------------------------------------------------------------------- */

void NCM_Service(void)
{
  if (s_notifyDue && (s_alt == 1U) && ((int32_t)(HAL_GetTick() - s_notifyDueMs) >= 0)) {
    s_notifyDue = false;
    sendSpeedNotification(&hUSBD_Device_CDC);
  }
}

bool NCM_LinkUp(void)
{
  return s_linkUp && (hUSBD_Device_CDC.dev_state == USBD_STATE_CONFIGURED);
}

static void parseBlock(const uint8_t *buf, uint16_t len,
                       void (*deliver)(const uint8_t *, uint16_t, void *), void *ctx)
{
  if ((len < NTH16_LEN) || (rd32(buf) != NTH16_SIGNATURE)) {
    return;
  }
  uint16_t ndp = rd16(&buf[10]);
  /* NDPs can chain; a well-behaved host uses one, so a handful bounds a malformed chain. */
  for (int chain = 0; (chain < 4) && (ndp != 0U); ++chain) {
    if (((ndp & 3U) != 0U) || ((uint32_t)ndp + NDP16_HEADER_LEN > len) ||
        (rd32(&buf[ndp]) != NDP16_SIGNATURE_NCM0)) {
      return;
    }
    const uint16_t ndpLen = rd16(&buf[ndp + 4U]);
    if ((ndpLen < NDP16_HEADER_LEN + 4U) || ((uint32_t)ndp + ndpLen > len)) {
      return;
    }
    for (uint16_t e = (uint16_t)(ndp + NDP16_HEADER_LEN); (uint32_t)e + 4U <= (uint32_t)ndp + ndpLen; e = (uint16_t)(e + 4U)) {
      const uint16_t off = rd16(&buf[e]);
      const uint16_t dlen = rd16(&buf[e + 2U]);
      if ((off == 0U) || (dlen == 0U)) {
        break;
      }
      if (((uint32_t)off + dlen <= len) && (dlen <= NCM_MAX_SEGMENT_SIZE)) {
        deliver(&buf[off], dlen, ctx);
      }
    }
    ndp = rd16(&buf[ndp + 6U]);
  }
}

void NCM_Receive(void (*deliver)(const uint8_t *frame, uint16_t len, void *ctx), void *ctx)
{
  /* Two passes: a block the interrupt completes while the first is being handed over is picked
   * up by the second; anything later waits for the next call. */
  for (int pass = 0; pass < 2; ++pass) {
    const uint8_t older = (uint8_t)(s_rxCur ^ 1U);
    if (s_rxLen[older] != 0U) {
      parseBlock(s_rxBuf[older], s_rxLen[older], deliver, ctx);
      s_rxLen[older] = 0U;
    }
    if (!s_rxArmed && (s_alt == 1U)) {
      /* Reception stopped with both buffers full; the interrupt is quiet on this endpoint
       * until we re-arm, so the current buffer is ours to drain. */
      const uint8_t cur = s_rxCur;
      if (s_rxLen[cur] != 0U) {
        parseBlock(s_rxBuf[cur], s_rxLen[cur], deliver, ctx);
        s_rxLen[cur] = 0U;
      }
      s_rxOff = 0U;
      s_rxBlockLen = 0U;
      rxArm(&hUSBD_Device_CDC);
    }
  }
}

static uint16_t ndpSize(uint8_t datagrams)
{
  /* header + one entry per datagram + the terminating null entry */
  return (uint16_t)(NDP16_HEADER_LEN + 4U * ((uint16_t)datagrams + 1U));
}

void NCM_TxKick(void)
{
  if (s_txBusy || (s_txCount == 0U) || !NCM_LinkUp()) {
    return;
  }

  uint8_t *buf = s_txBuf[s_txFill];
  const uint16_t ndp = (uint16_t)ALIGN_UP(s_txEnd, NCM_ALIGN);
  const uint16_t ndpLen = ndpSize(s_txCount);
  const uint16_t blockLen = (uint16_t)(ndp + ndpLen);

  wr32(&buf[ndp], NDP16_SIGNATURE_NCM0);
  wr16(&buf[ndp + 4U], ndpLen);
  wr16(&buf[ndp + 6U], 0U);                       /* wNextNdpIndex */
  uint16_t e = (uint16_t)(ndp + NDP16_HEADER_LEN);
  for (uint8_t i = 0U; i < s_txCount; ++i, e = (uint16_t)(e + 4U)) {
    wr16(&buf[e], s_txOff[i]);
    wr16(&buf[e + 2U], s_txDgLen[i]);
  }
  wr16(&buf[e], 0U);
  wr16(&buf[e + 2U], 0U);

  wr32(&buf[0], NTH16_SIGNATURE);
  wr16(&buf[4], NTH16_LEN);
  wr16(&buf[6], s_txSeq++);
  wr16(&buf[8], blockLen);
  wr16(&buf[10], ndp);

  s_txBusy = true;
  hUSBD_Device_CDC.ep_in[NCM_IN_EP & 0x0FU].total_length = blockLen;
  (void)USBD_LL_Transmit(&hUSBD_Device_CDC, NCM_IN_EP, buf, blockLen);

  s_txFill ^= 1U;
  s_txEnd = NTH16_LEN;
  s_txCount = 0U;
}

static bool txFits(uint16_t len)
{
  const uint32_t off = ALIGN_UP((uint32_t)s_txEnd, NCM_ALIGN);
  const uint32_t end = ALIGN_UP(off + len, NCM_ALIGN) + ndpSize((uint8_t)(s_txCount + 1U));
  return (s_txCount < NCM_TX_MAX_DATAGRAMS) && (end <= s_ntbInMax);
}

uint8_t *NCM_TxReserve(uint16_t len)
{
  if (!NCM_LinkUp() || (len == 0U) || (len > NCM_MAX_SEGMENT_SIZE)) {
    return NULL;
  }
  if (!txFits(len)) {
    /* The block being assembled is full: send it as soon as the endpoint is free. TCP sends in
     * bursts, so waiting briefly here is much cheaper than dropping and retransmitting. */
    const uint32_t start = HAL_GetTick();
    while (s_txBusy && NCM_LinkUp() && ((HAL_GetTick() - start) < NCM_TX_WAIT_MS)) {
    }
    NCM_TxKick();
    if (!txFits(len)) {
      return NULL;
    }
  }
  s_txPendingOff = (uint16_t)ALIGN_UP(s_txEnd, NCM_ALIGN);
  s_txPendingLen = len;
  return &s_txBuf[s_txFill][s_txPendingOff];
}

void NCM_TxCommit(void)
{
  s_txOff[s_txCount] = s_txPendingOff;
  s_txDgLen[s_txCount] = s_txPendingLen;
  s_txCount++;
  s_txEnd = (uint16_t)(s_txPendingOff + s_txPendingLen);
}

/* --- addresses --------------------------------------------------------------------------- */

void NCM_HostMac(uint8_t mac[6])
{
  /* FNV-1a over the 96-bit UID, spread over five bytes after a locally administered, unicast
   * first octet. */
  uint32_t h = 2166136261UL;
  const uint32_t uid[3] = {*(uint32_t *)DEVICE_ID1, *(uint32_t *)DEVICE_ID2, *(uint32_t *)DEVICE_ID3};
  const uint8_t *b = (const uint8_t *)uid;
  for (unsigned i = 0; i < sizeof(uid); ++i) {
    h ^= b[i];
    h *= 16777619UL;
  }
  mac[0] = 0x02U;
  mac[1] = (uint8_t)(h >> 24);
  mac[2] = (uint8_t)(h >> 16);
  mac[3] = (uint8_t)(h >> 8);
  mac[4] = (uint8_t)h;
  mac[5] = 0x02U;
}

void NCM_DeviceMac(uint8_t mac[6])
{
  NCM_HostMac(mac);
  mac[5] = 0x01U;
}

#endif /* USBCON */
