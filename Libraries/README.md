In order to use these libraries they must be linked (individually) into the libraries directory within your Arduino folder.

| Library | Contents |
|---------|----------|
| `HMTLprotocol` | Header-only. `HMTLProtocol.h` is the serial command protocol; `HMTLWireFormat.h` is **the** definition of the HMTL wire format — `msg_hdr_t`, the `MSG_TYPE_*`/`MSG_FLAG_*` codes, the `msg_*` payload structs, `config_hdr_*`, `output_hdr_t`, the `HMTL_OUTPUT_*`/`HMTL_PROGRAM_*` codes. Deliberately transport- and platform-agnostic (`<stdint.h>` + `Socket.h`, never `Arduino.h`), so non-Arduino consumers — host tools, native unit tests, the WLED RS485 bridge usermod — can import it rather than copy the declarations. |
| `HMTLTypes` | Module configuration runtime (EEPROM config read/write/validate/print). Includes `HMTLWireFormat.h`. Its sources need PixelUtil/FastLED, EEPromUtils, MPR121 and XBeeSocket. |
| `HMTLMessaging` | Message formatting/handling over a `Socket` transport, plus the HMTL program modules (`HMTLPrograms`, `ProgramManager`, `MessageHandler`). Includes `HMTLWireFormat.h`. Needs RS485Utils, Debug and PixelUtil/FastLED. |
| `HMTLPoofer` | Poofer/flame-effect module support. |
| `TimeSync` | `MSG_TYPE_TIMESYNC` clock synchronisation across modules. |

If all you need is to speak the protocol, depend on `HMTLprotocol` alone.

**Linking requirement (changed):** `HMTLTypes` and `HMTLMessaging` used to be
satisfiable on their own — `HMTLTypes.h` was include-free and `HMTLMessaging`
reached `socket_addr_t` through RS485Utils. They now both `#include
"HMTLWireFormat.h"`, so **`HMTLprotocol` and `Socket` (from
[aphelps/ArduinoLibs](https://github.com/aphelps/ArduinoLibs), which provides
`socket_addr_t`) must also be linked into your Arduino libraries directory**
wherever `HMTLTypes` or `HMTLMessaging` is used. The Arduino IDE and the
PlatformIO library dependency finder resolve the transitive include
automatically, but only if the library is actually present; linking just the old
set gives a missing-header error on `HMTLWireFormat.h` or `Socket.h`.

**Wire structs are packed.** Every struct in `HMTLWireFormat.h` carries
`__attribute__((__packed__))` so that an ATMega328 module and an ESP32 module —
which share a bus and read each other's config blobs — agree on every size and
field offset. Without it, `config_hdr_v2_t` is 8 B on AVR but 10 B on 32-bit
targets (interior padding before `address`) and `msg_poll_response_t` is 15 B
versus 16 B, which made `HMTL_MSG_POLL_MIN_LEN` 23 or 24 depending on the
target. Packing is layout-neutral on AVR, so deployed modules are unaffected.
`platformio/HMTL_Test/test/test_wire_format` pins every size and offset; run
`pio test -e native` from `platformio/HMTL_Test` (no external include paths
needed — the desktop stubs are vendored in `stubs/`).