In order to use these libraries they must be linked (individually) into the libraries directory within your Arduino folder.

| Library | Contents |
|---------|----------|
| `HMTLprotocol` | Header-only. `HMTLProtocol.h` is the serial command protocol; `HMTLWireFormat.h` is **the** definition of the HMTL wire format — `msg_hdr_t`, the `MSG_TYPE_*`/`MSG_FLAG_*` codes, the `msg_*` payload structs, `config_hdr_*`, `output_hdr_t`, the `HMTL_OUTPUT_*`/`HMTL_PROGRAM_*` codes. Deliberately transport- and platform-agnostic (`<stdint.h>` + `Socket.h`, never `Arduino.h`), so non-Arduino consumers — host tools, native unit tests, the WLED RS485 bridge usermod — can import it rather than copy the declarations. |
| `HMTLTypes` | Module configuration runtime (EEPROM config read/write/validate/print). Its sources need PixelUtil/FastLED, EEPromUtils, MPR121 and XBeeSocket. |
| `HMTLMessaging` | Message formatting/handling over a `Socket` transport, plus the HMTL program modules (`HMTLPrograms`, `ProgramManager`, `MessageHandler`). Needs RS485Utils, Debug and PixelUtil/FastLED. |
| `HMTLPoofer` | Poofer/flame-effect module support. |
| `TimeSync` | `MSG_TYPE_TIMESYNC` clock synchronisation across modules. |

If all you need is to speak the protocol, depend on `HMTLprotocol` alone.