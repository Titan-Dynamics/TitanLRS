#!/usr/bin/env python3
"""
titan_usbcfg — host CLI for the TitanLRS STM32 USB config API.

This is the bring-up tool for the firmware side of lib/USBConfig (before any web code exists)
and the regression tool afterwards. It speaks the exact protocol described in
lib/USBConfig/usbcfg_protocol.h.

Requires pyserial (`pip install pyserial`).

Examples
--------
    ./titan_usbcfg.py hello
    ./titan_usbcfg.py get -o config.json
    ./titan_usbcfg.py get --export -o models.json
    ./titan_usbcfg.py set models.json
    ./titan_usbcfg.py set '{"serial-protocol": 0}'
    ./titan_usbcfg.py reset --config
    ./titan_usbcfg.py reboot
    ./titan_usbcfg.py soak 100

The port is auto-detected by USB VID:PID 0483:5740; override with `-p /dev/tty.usbmodemXXXX`.
"""

import argparse
import json
import struct
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    sys.exit("pyserial is required: pip install pyserial")

USB_VID = 0x0483
USB_PID = 0x5740

PROTOCOL_VERSION = 1
CHUNK_MAX = 1000

TCFG_HELLO = 0x5443
TCFG_BYE = 0x5444
TCFG_GET = 0x5445
TCFG_SET = 0x5446
TCFG_REBOOT = 0x5447
TCFG_RESET = 0x5448
TCFG_PING = 0x5449

RES_CONFIG = 0
RES_OPTIONS = 1

GETFLAG_EXPORT = 1 << 0
RESETFLAG_CONFIG = 1 << 0
RESETFLAG_OPTIONS = 1 << 1

CHUNK_FIRST = 1 << 0
CHUNK_LAST = 1 << 1

ERR_NAMES = {
    1: "UNSUPPORTED",
    2: "BUSY",
    3: "BAD_REQUEST",
    4: "TOO_LARGE",
    5: "PARSE",
    6: "NO_SESSION",
    7: "INTERNAL",
}

FEATURE_NAMES = {
    1 << 0: "options-write",
    1 << 1: "cw",
    1 << 2: "lr1121-update",
}


def crc8_dvb_s2(crc, byte):
    crc ^= byte
    for _ in range(8):
        crc = ((crc << 1) ^ 0xD5) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def crc8_over(data):
    crc = 0
    for b in data:
        crc = crc8_dvb_s2(crc, b)
    return crc


class DeviceError(Exception):
    def __init__(self, code, message):
        super().__init__("%s: %s" % (ERR_NAMES.get(code, "ERR%d" % code), message))
        self.code = code
        self.message = message


class UsbConfigSession:
    def __init__(self, port, baud=460800, timeout=2.0, verbose=False):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.timeout = timeout
        self.verbose = verbose
        self.hello = None
        self.features = 0
        self._rx = bytearray()

    # -- framing -----------------------------------------------------------------
    def _encode(self, function, payload=b""):
        body = bytes([0]) + struct.pack("<HH", function, len(payload)) + payload
        return b"$X<" + body + bytes([crc8_over(body)])

    def send(self, function, payload=b""):
        frame = self._encode(function, payload)
        if self.verbose:
            print(">>> fn=0x%04X len=%d" % (function, len(payload)), file=sys.stderr)
        self.ser.write(frame)
        self.ser.flush()

    def read_frame(self, timeout=None):
        """Return (direction, function, payload) for the next well-formed frame."""
        deadline = time.time() + (self.timeout if timeout is None else timeout)
        while True:
            chunk = self.ser.read(512)
            if chunk:
                self._rx.extend(chunk)
                frame = self._try_parse()
                if frame is not None:
                    return frame
            elif time.time() > deadline:
                raise TimeoutError("no response from device")

    def _try_parse(self):
        buf = self._rx
        while True:
            start = buf.find(b"$X")
            if start < 0:
                # Keep a trailing '$' in case the 'X' has not arrived yet.
                del buf[: max(0, len(buf) - 1)]
                return None
            if start:
                del buf[:start]
            if len(buf) < 8:
                return None
            direction = chr(buf[2])
            if direction not in ">!":
                del buf[:2]
                continue
            function, size = struct.unpack_from("<HH", buf, 4)
            if len(buf) < 8 + size + 1:
                return None
            body = bytes(buf[3 : 8 + size])
            crc = buf[8 + size]
            payload = bytes(buf[8 : 8 + size])
            del buf[: 9 + size]
            if crc != crc8_over(body):
                if self.verbose:
                    print("!!! bad CRC, dropping frame", file=sys.stderr)
                continue
            if self.verbose:
                print("<<< %s fn=0x%04X len=%d" % (direction, function, size), file=sys.stderr)
            return direction, function, payload

    def request(self, function, payload=b""):
        """Send a command and return the payload of the matching success frame."""
        self.send(function, payload)
        while True:
            direction, fn, resp = self.read_frame()
            if fn != function:
                continue  # stale frame from an earlier request
            if direction == "!":
                raise DeviceError(resp[0] if resp else 0, resp[1:].decode("ascii", "replace"))
            return resp

    # -- session -----------------------------------------------------------------
    def open(self):
        resp = self.request(TCFG_HELLO, bytes([PROTOCOL_VERSION]))
        if len(resp) < 5:
            raise DeviceError(3, "short HELLO response")
        proto = resp[0]
        self.features = struct.unpack_from("<I", resp, 1)[0]
        self.hello = json.loads(resp[5:].decode("utf-8"))
        self.hello["proto-version"] = proto
        return self.hello

    def close(self):
        try:
            self.request(TCFG_BYE)
        except (TimeoutError, DeviceError, OSError):
            pass
        self.ser.close()

    def ping(self):
        self.request(TCFG_PING)

    # -- endpoints ---------------------------------------------------------------
    def get(self, resource=RES_CONFIG, export=False):
        flags = GETFLAG_EXPORT if export else 0
        self.send(TCFG_GET, bytes([resource, flags]))
        out = bytearray()
        expect_seq = 0
        total = None
        while True:
            direction, fn, payload = self.read_frame()
            if fn != TCFG_GET:
                continue
            if direction == "!":
                raise DeviceError(payload[0] if payload else 0,
                                  payload[1:].decode("ascii", "replace"))
            seq, cflags = struct.unpack_from("<HB", payload, 0)
            off = 3
            if cflags & CHUNK_FIRST:
                total = struct.unpack_from("<I", payload, off)[0]
                off += 4
            if seq != expect_seq:
                raise DeviceError(3, "chunk out of order (got %d want %d)" % (seq, expect_seq))
            expect_seq += 1
            out.extend(payload[off:])
            if cflags & CHUNK_LAST:
                break
        if total is not None and len(out) != total:
            raise DeviceError(3, "length mismatch: got %d, declared %d" % (len(out), total))
        return json.loads(out.decode("utf-8"))

    def set(self, document, resource=RES_CONFIG):
        data = json.dumps(document, separators=(",", ":")).encode("utf-8")
        total = len(data)
        seq = 0
        offset = 0
        while True:
            first = offset == 0
            room = CHUNK_MAX
            piece = data[offset : offset + room]
            offset += len(piece)
            last = offset >= total
            header = struct.pack("<BHB", resource, seq,
                                 (CHUNK_FIRST if first else 0) | (CHUNK_LAST if last else 0))
            if first:
                header += struct.pack("<I", total)
            self.send(TCFG_SET, header + piece)
            direction, fn, payload = self.read_frame()
            while fn != TCFG_SET:
                direction, fn, payload = self.read_frame()
            if direction == "!":
                raise DeviceError(payload[0] if payload else 0,
                                  payload[1:].decode("ascii", "replace"))
            if last:
                # final response = status(1) + ascii message
                return payload[1:].decode("ascii", "replace")
            seq += 1

    def reboot(self):
        self.request(TCFG_REBOOT)

    def reset(self, config=True, options=False):
        flags = (RESETFLAG_CONFIG if config else 0) | (RESETFLAG_OPTIONS if options else 0)
        self.request(TCFG_RESET, bytes([flags]))


def find_port():
    for p in list_ports.comports():
        if p.vid == USB_VID and p.pid == USB_PID:
            return p.device
    return None


def describe_features(mask):
    names = [n for bit, n in FEATURE_NAMES.items() if mask & bit]
    return ", ".join(names) if names else "(none)"


def cmd_hello(sess, args):
    print(json.dumps(sess.hello, indent=2))
    print("features: 0x%08X  %s" % (sess.features, describe_features(sess.features)))


def cmd_get(sess, args):
    doc = sess.get(RES_OPTIONS if args.options else RES_CONFIG, export=args.export)
    text = json.dumps(doc, indent=2)
    if args.output:
        with open(args.output, "w") as fh:
            fh.write(text + "\n")
        print("wrote %s (%d bytes)" % (args.output, len(text)))
    else:
        print(text)


def cmd_set(sess, args):
    source = args.document
    try:
        with open(source) as fh:
            doc = json.load(fh)
    except (OSError, IOError):
        doc = json.loads(source)
    print(sess.set(doc))


def cmd_reboot(sess, args):
    sess.reboot()
    print("rebooting")


def cmd_reset(sess, args):
    if not args.config and not args.options:
        args.config = True
    sess.reset(config=args.config, options=args.options)
    print("reset complete, rebooting")


def cmd_soak(sess, args):
    """N get/set/verify cycles — the regression test for the session mux."""
    failures = 0
    started = time.time()
    for i in range(args.count):
        try:
            before = sess.get()
            cfg = before.get("config", {})
            if sess.hello.get("module-type") == "RX":
                probe = {"serial-protocol": cfg.get("serial-protocol", 0)}
            else:
                probe = {"button-actions": cfg.get("button-actions", [])}
            sess.set(probe)
            after = sess.get()
            for key in probe:
                if after.get("config", {}).get(key) != before.get("config", {}).get(key):
                    raise DeviceError(7, "round-trip mismatch on '%s'" % key)
        except (DeviceError, TimeoutError) as exc:
            failures += 1
            print("cycle %d FAILED: %s" % (i, exc))
            if args.stop_on_error:
                break
        if (i + 1) % 10 == 0:
            print("... %d/%d cycles, %d failures" % (i + 1, args.count, failures))
    elapsed = time.time() - started
    print("soak done: %d cycles, %d failures, %.1fs (%.0f ms/cycle)"
          % (args.count, failures, elapsed, 1000 * elapsed / max(1, args.count)))
    return 1 if failures else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-p", "--port", help="serial port (default: auto-detect 0483:5740)")
    ap.add_argument("-b", "--baud", type=int, default=460800, help="nominal CDC baud rate")
    ap.add_argument("-t", "--timeout", type=float, default=2.0, help="per-request timeout (s)")
    ap.add_argument("-v", "--verbose", action="store_true", help="trace frames on stderr")
    sub = ap.add_subparsers(dest="command", required=True)

    sub.add_parser("hello", help="open a session and print device identity").set_defaults(fn=cmd_hello)

    g = sub.add_parser("get", help="read the config document")
    g.add_argument("--export", action="store_true", help="mirror the HTTP ?export argument")
    g.add_argument("--options", action="store_true", help="read the options resource instead")
    g.add_argument("-o", "--output", help="write JSON to this file instead of stdout")
    g.set_defaults(fn=cmd_get)

    s = sub.add_parser("set", help="write a config document (file path or inline JSON)")
    s.add_argument("document")
    s.set_defaults(fn=cmd_set)

    sub.add_parser("reboot", help="reboot the device").set_defaults(fn=cmd_reboot)

    r = sub.add_parser("reset", help="reset config/options to defaults and reboot")
    r.add_argument("--config", action="store_true")
    r.add_argument("--options", action="store_true")
    r.set_defaults(fn=cmd_reset)

    k = sub.add_parser("soak", help="N get/set/verify cycles")
    k.add_argument("count", type=int)
    k.add_argument("--stop-on-error", action="store_true")
    k.set_defaults(fn=cmd_soak)

    args = ap.parse_args(argv)

    port = args.port or find_port()
    if not port:
        sys.exit("no TitanLRS device found (looked for USB %04X:%04X); pass -p PORT"
                 % (USB_VID, USB_PID))

    sess = UsbConfigSession(port, args.baud, args.timeout, args.verbose)
    try:
        sess.open()
        rc = args.fn(sess, args)
    except (DeviceError, TimeoutError) as exc:
        sys.exit("error: %s" % exc)
    finally:
        try:
            sess.close()
        except Exception:
            pass
    return rc or 0


if __name__ == "__main__":
    sys.exit(main())
