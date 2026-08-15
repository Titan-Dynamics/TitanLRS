/*
  USBVendorStream — Arduino Stream over the vendor-class bulk pipe.

  Same shape as USBSerial, minus everything that only means something on a
  serial port (baud, line coding, DTR/RTS). lib/USBConfig talks to a `Stream *`,
  so the config service needs no knowledge of which transport it is on.

  There is no begin()-per-port notion here: both USB functions enumerate
  together as one composite device, so begin() brings the device up if it is not
  already up, exactly as USBSerial::begin() does.
*/

#ifndef _USBVENDORSTREAM_H_
#define _USBVENDORSTREAM_H_

#if defined(USBCON) && defined(USBD_USE_CDC)

#include "Stream.h"
#include "usbd_composite_if.h"

class USBVendorStream : public Stream {
  public:
    void begin(void);
    void end(void);

    virtual int available(void);
    virtual int availableForWrite(void);
    virtual int peek(void);
    virtual int read(void);
    virtual size_t readBytes(char *buffer, size_t length);
    size_t readBytes(uint8_t *buffer, size_t length)
    {
      return readBytes((char *)buffer, length);
    }
    virtual void flush(void);
    virtual size_t write(uint8_t);
    virtual size_t write(const uint8_t *buffer, size_t size);
    using Print::write;

    /* True once the device is configured. Note this cannot tell whether a host
     * has actually claimed the interface — see VCFG_connected(). */
    operator bool(void);
};

extern USBVendorStream SerialCfg;

#endif /* USBCON && USBD_USE_CDC */
#endif /* _USBVENDORSTREAM_H_ */
