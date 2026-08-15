/*
  USBVendorStream — Arduino Stream over the vendor-class bulk pipe.
  Mirrors USBSerial.cpp (Copyright (c) 2015 Arduino LLC) against the VCFG queues.
*/

#if defined(USBCON) && defined(USBD_USE_CDC)

#include "USBVendorStream.h"
#include "wiring.h"

USBVendorStream SerialCfg;

void USBVendorStream::begin(void)
{
  USBComposite_init();
}

void USBVendorStream::end(void)
{
  // Tears down the composite device, and therefore Serial too.
  USBComposite_deInit();
}

int USBVendorStream::availableForWrite()
{
  return static_cast<int>(CDC_TransmitQueue_WriteSize(&VCFG_TransmitQueue));
}

size_t USBVendorStream::write(uint8_t ch)
{
  return write(&ch, 1);
}

size_t USBVendorStream::write(const uint8_t *buffer, size_t size)
{
  size_t rest = size;
  // See USB_TX_BLOCKING_LIMIT_MS: the host releasing the interface (which is
  // exactly what Disconnect does) leaves the queue undrainable, and without an
  // absolute deadline this loop stalls the main loop until a host comes back.
  const uint32_t deadline = millis() + USB_TX_BLOCKING_LIMIT_MS;
  while (rest > 0 && VCFG_connected()) {
    auto portion = (size_t)CDC_TransmitQueue_WriteSize(&VCFG_TransmitQueue);
    if (rest < portion) {
      portion = rest;
    }
    if (portion > 0) {
      // TS: only the main thread writes, so the write position is ours alone.
      CDC_TransmitQueue_Enqueue(&VCFG_TransmitQueue, buffer, portion);
      rest -= portion;
      buffer += portion;
      VCFG_continue_transmit();
    } else if ((int32_t)(millis() - deadline) >= 0) {
      break;    // queue full and nobody is reading the pipe: drop the tail
    }
  }
  return size - rest;
}

int USBVendorStream::available(void)
{
  return static_cast<int>(CDC_ReceiveQueue_ReadSize(&VCFG_ReceiveQueue));
}

int USBVendorStream::read(void)
{
  auto ch = CDC_ReceiveQueue_Dequeue(&VCFG_ReceiveQueue);
  VCFG_resume_receive();
  return ch;
}

size_t USBVendorStream::readBytes(char *buffer, size_t length)
{
  uint16_t read;
  auto rest = static_cast<uint16_t>(length);
  _startMillis = millis();
  do {
    read = CDC_ReceiveQueue_Read(&VCFG_ReceiveQueue, reinterpret_cast<uint8_t *>(buffer), rest);
    VCFG_resume_receive();
    rest -= read;
    buffer += read;
    if (rest == 0) {
      return length;
    }
  } while (millis() - _startMillis < _timeout);
  return length - rest;
}

int USBVendorStream::peek(void)
{
  return CDC_ReceiveQueue_Peek(&VCFG_ReceiveQueue);
}

void USBVendorStream::flush(void)
{
  // Bounded: the queue only drains when the host reads the IN endpoint, so an
  // unbounded wait here hangs the main loop for as long as nobody is listening.
  const uint32_t deadline = millis() + USB_TX_BLOCKING_LIMIT_MS;
  while (CDC_TransmitQueue_ReadSize(&VCFG_TransmitQueue) > 0) {
    if ((int32_t)(millis() - deadline) >= 0) {
      break;
    }
  }
}

USBVendorStream::operator bool()
{
  return VCFG_connected();
}

#endif // USBCON && USBD_USE_CDC
