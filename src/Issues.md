# TitanLRS — issue log

> NOTE: this file was missing from the tree when BUG #5 was opened (2026-08-12) — the BUG #1–#4
> writeups referenced by `CLAUDE.md` are not in git history at this path and were presumably kept
> locally. Recreated here so the ledger protocol in `CLAUDE.md` has somewhere to live.

---

## BUG #5 — STM32 RX: config save over USB is slow and often times out — OPEN

**Reported:** 2026-08-12. **Status:** cause not yet identified; one contributing defect found and fixed.

### Symptom

On an RX, changing a setting in the web dashboard and confirming *Save & Reboot* hangs for several
seconds and then fails with "Device did not respond". The setting is **not** applied — reconnecting
shows the old value. The same operation on a TX is effectively instant.

Reporter's device: `TD_LR2021_RX`, serial protocol **MAVLink**, force-tlm enabled, firmware
`4624d1`.

### Evidence ledger

Measured directly against the reporter's RX (`TD_LR2021_RX`, fw `4624d1`, **no TX powered**, so the
RF/serial-load leads are dead) with `python/titan_usbcfg.py`:

| # | Test | Result |
|---|---|---|
| 1 | `hello` | works, instant |
| 2 | `get-options`, `get` (config) | work |
| 3 | `soak 20` — config GET/SET round-trips, tiny payloads | **20/20 pass, 178 ms/cycle** |
| 4 | `soak 20 --options` — 145-byte SET payload | **20/20 fail**, ~2 s each (host timeout) |
| 5 | `set-options --replace` with a 51-byte payload | **passes** (and commits to flash) |
| 6 | Same, padded to sweep the size | **passes ≤ 127 bytes on the wire, fails ≥ 131** |
| 7 | Port presence watched across a failing SET | port never disappears — **the device does not reset** |
| 8 | Retry `hello` after a failing large SET | sometimes recovers after ~3 s (the session timeout); after repeated large SETs it stops responding entirely for 20 s+ while staying enumerated |
| 9 | RX LED while "unresponsive" | still blinking at the no-connection rate — `devicesUpdate()` is running, so **the main loop is alive**; the device is mute, not hung |
| 10 | Same 167-byte frame written as one burst vs. in 64 B / 32 B pieces 20 ms apart | burst **TIMEOUT**, paced **PASS**, **PASS** — identical bytes, so the payload, the parser and `ConfigJson_ApplyOptions` are all exonerated; only the arrival pattern matters |

**The resource is a red herring — it is payload size.** Options SETs failed only because that
document (145 B) is bigger than the config probe the soak used. The break is at **128 bytes on the
wire = 2 × the 64-byte USB FS packet**: a request that fits in two packets works, three packets
never completes. The reporter's failing save is a two-step `saveOptions` → `saveConfig`; the
options half is the first over the line, so nothing is applied.

### What that rules in

The device stops answering *without* resetting (row 7) and the parser is left starved mid-frame
(row 8 — it recovers when `USBCFG_SESSION_TIMEOUT_MS` expires), so inbound bytes are being lost or
reception is not being resumed after the CDC receive queue fills. Relevant core facts:

- `CDC_RECEIVE_QUEUE_BUFFER_SIZE` = `USB_FS_MAX_PACKET_SIZE × 3` = 192 B, and
  `CDC_ReceiveQueue_ReserveBlock()` needs a whole free packet, so ~128 B is the usable depth before
  reception pauses. That matches the measured threshold exactly.
- `USBSerial::read()` does call `CDC_resume_receive()`, so the drain loop should un-pause it.
- `USBSerial::write()` loops `while (rest > 0 && CDC_connected())`, and `CDC_connected()` is false
  once a transmit has been outstanding for `USB_CDC_TRANSMIT_TIMEOUT` = **3 ms** — so a stalled
  transmit makes the device silently drop every response until the host reopens the port. This
  matches the "alive but mute until reconnect" shape and is worth checking as the failure mode for
  the *response*, separately from the lost request bytes.

### Ruled out (each with the specific refuter — do not re-propose without new evidence)

| Hypothesis | Refuted by |
|---|---|
| RX protocol UART fighting the config service for the CDC port | On STM32 `SERIAL_PROTOCOL_TX` is `HardwareSerial(USART1)`; `Serial` (CDC) is never the protocol port (`rx_main.cpp:138-147`) |
| Debug / backpack output corrupting the CDC stream | Every `Serial.write` on RX is behind `DEBUG_LOG` / `DEBUG_ENABLED`; `BackpackOrLogStrm` is a `NullStream` in the shipped build |
| `reconfigureSerial()` tearing the CDC down mid-session | `serialShutdown()` ends `SERIAL_PROTOCOL_TX`, not `Serial`, on STM32 (`rx_main.cpp:1538-1551`) |
| Committing config from the USB path with the radio live | The TX does exactly the same thing and is reliable (and rows 3/5: small SETs commit to flash and pass) |
| RX serial-protocol input using the same blocking-read pattern | `SerialIO::processSerialInput()` reads `min(available(), maxBytes)` (`rx-serial/SerialIO.cpp:12`) |
| MAVLink / RF load on the RX | No TX was ever powered during any of this — the RX is unlinked |
| Anything specific to the options resource, or to `ConfigJson_ApplyOptions` | Row 5: a small options SET applies and commits correctly. Row 6: the same document padded past 127 B fails |
| A crash / watchdog reset on the RX | Row 7: the USB port never re-enumerates |
| The MSPv2 parser (`UsbCfgParser::feed`) | Byte-fed state machine, correct across call boundaries, `USBCFG_PAYLOAD_MAX` = 1024 |

### Root cause

Rows 9-13 pin it down:

| # | Test | Result |
|---|---|---|
| 11 | Same 167 B frame, burst vs. paced (repeated on the block-read build) | burst still fails — **the byte-at-a-time drain was not the cause**; that hypothesis is refuted |
| 12 | Did a failed burst apply anyway? | no — `rcvr-uart-baud` unchanged, so the **request is lost inbound**, it is not a lost reply |
| 13 | After a failed burst, 12 paced 9-byte PINGs on the **same open port** over 6 s | **total silence** — a desynced parser would have resynced within a few frames, so **reception is permanently paused** until the port is reopened |

The STM32duino CDC receive queue is `USB_FS_MAX_PACKET_SIZE × CDC_RECEIVE_QUEUE_BUFFER_PACKET_NUMBER`
= 64 × 3 = 192 bytes, and `CDC_ReceiveQueue_ReserveBlock()` will only hand out a block when a whole
64-byte packet still fits. Two packets in and it returns NULL, so `CDC_resume_receive()` stops
re-arming the OUT endpoint — and on this core that pause is never lifted again, even though the
application drains the queue and `USBSerial::read()`/`readBytes()` both call `CDC_resume_receive()`
afterwards. Anything over ~127 bytes on the wire therefore kills the port until it is reopened.

The TX escaped it only by draining every loop iteration instead of every 10 ms, so the queue never
reached the pause point in normal use.

**Fix:** `-D CDC_RECEIVE_QUEUE_BUFFER_PACKET_NUMBER=32` in `env_common_stm32` — a 2 KB receive
queue, verified in the map file (`ReceiveQueue` 198 B → 2054 B). The largest frame the protocol can
send is `USBCFG_CHUNK_MAX` + headers = 1017 B, and the host waits for a per-chunk ack, so no more
than one chunk is ever in flight: the queue can no longer fill, and the broken pause path is never
entered.

**This avoids the core defect rather than repairing it.** The resume-after-pause path in
`libraries/USBDevice` remains broken; if a future feature streams more than 2 KB at the RX without
waiting for acks it will hit the same wall. The durable fix is to drain the port from the RX main
loop the way `tx_main` does, instead of from a 10 ms device hook.

### Next step

The device is unresponsive when this happens but stays powered, which is the ideal state for the
SWD halt-and-inspect documented in `CLAUDE.md` — one halt gives the PC/stack and says whether the
main loop is spinning (and where) or whether it is alive and dropping writes. No ST-Link is
currently wired to the RX.

Failing that: `-DCDC_RECEIVE_QUEUE_BUFFER_PACKET_NUMBER=n` (the core guards it with `#ifndef`)
enlarges the receive queue and would move the threshold — useful as a *diagnostic* to confirm the
mechanism, not as a fix on its own, since a big models import would still exceed any fixed size.
