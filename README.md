HMTL
====

## History

This project was originally started to develop a hardware/software platform for controlling 12V LED strips and 12V powered propane flame effects for a Burning Man art car ([Ku, the Heavy Metal Tiki Lounge/God](https://www.facebook.com/KuHMTL)), however the capabilities of this project have now expanded well past that.

## Overview

This project contains protocols designed to make it easy to build networked modules which read sensors and can activate various external devices.  Currently supported types include:

  * Value (Single-color LEDs, solenoids and ignitors for flame effects)
  * RGB (RGB LEDs and LED strips)
  * Pixels (WS2801 style LEDs, easily expandable to any type supported by the [FastLED](https://github.com/FastLED/FastLED) library)
  * MPR121 (MPR121 capacitive touch sensors)
  * RS485 (RS485 communication, such as ST485 or MAX485 chips)

The configuration for these modules are stored in EEPROM, is loaded during module startup, and make initializes these devices when read.

A general purpose protocol is used to communicate with the devices over any supported hardware layer, which currently includes RS485 for longer-distance two-wire connections and standard serial connections for communication via USB, Bluetooth modules such as [Adafruit's Bluefruit EZ-Link](http://www.adafruit.com/product/1588), or serial-enabled wireless devices such as XBee radios.

## Hardware

While many features of these libraries can be used with generic Arduinos or breadboarded project, several special purpose PCBs have been designed for use with this code:

* Trigger Module
* HMTL Controller

## Example projects
* [HMTL Fire Control box](https://github.com/HMTL/HMTL_Fire_Control)
* [Adam's Cube and Triangle lights](https://github.com/aphelps/ObjectLights)

Getting started
---------------

In order to compile these sketches the contents of the Libraries must be linked to from within your Arduino/libaries directory, in addition several dependent libraries must also be installed:

Several additional libraries must be installed to make use of this project:
* [MPR121, PixelUtils, etc](https://github.com/aphelps/ArduinoLibs)
* [Nick Gammon's RS485 library](http://www.gammon.com.au/Arduino/RS485_non_blocking.zip)
* [FastLED](https://github.com/FastLED/FastLED)

Python tools
------------

There are python libraries provided for communicating with the modules and several command line utilities:
* [TailArduino.py](python/TailArduino.py): Reads from a serial connection and prints to the console
* [HMTLConfig.py](python/HMTLConfig.py): Used to write module configurations (along with the HMTLPythonConfig firmware)
* [HMTLCommandServer.py](python/HMTLCommandServer.py): Server that connects to a module and forwards commands received over an IP socket
* [HMTLClient.py](python/HMTLClient.py): Send commands to a command server
* [Scan.py](python/Scan.py): Send out polling commands via a command server to find all connected modules
* [HMTLWebClient.py](python/HMTLWebClient.py): Present a web page to control modules connected to a command server

Testing
-------

Build-time testing does not require a physical device. Three complementary approaches are available, in order of increasing fidelity:

### Track 1 — Python emulator

A software HMTL module that accepts the same messages as real hardware, loaded from any JSON config file in `python/configs/`. Useful for interactive development and automated protocol tests.

**Prerequisites:** `pip install -e python/`

```bash
# Start the emulator with a config file
python python/bin/HMTLEmulator python/configs/BigCube.json

# In another terminal, send commands with the existing tools unchanged
python python/bin/HMTLClient --blink -C 500,255,0,0,500,0,0,0 -O 0 --tcpsocket
python python/bin/HMTLClient --sequence -C 0:500:255,1:300:128 --tcpsocket
```

Run automated integration tests:
```bash
cd python
pytest hmtl/tests/test_emulator.py -v
```

### Track 2 — C++ unit tests (PlatformIO native)

Tests pure firmware logic (message routing, program state machines, config validation) compiled for the desktop. No hardware or emulator required.

**Prerequisites:** PlatformIO CLI (`pip install platformio`)

```bash
cd platformio/HMTL_Test
pio test -e native
```

Tests live in `platformio/HMTL_Test/test/`. Arduino hardware dependencies are satisfied by thin stubs in `platformio/HMTL_Test/stubs/` so the library code compiles and runs on the host.

### Track 3 — Full AVR firmware emulation (simavr)

Runs the actual compiled ATmega328 firmware binary inside [simavr](https://github.com/buserror/simavr), exercising real AVR timers, interrupts, and EEPROM. PlatformIO handles building and launching automatically.

**Prerequisites:** `brew install simavr` (macOS) or `apt install simavr` (Linux). macOS and Linux only.

```bash
cd platformio/HMTL_Module
pio test -e simavr_nano
```

UART output from the firmware (including `DEBUG_*` messages) appears via simavr's pseudo-terminal. For GDB debugging:

```bash
simavr -g -m atmega328p .pio/build/simavr_nano/firmware.elf &
avr-gdb -ex "target remote :1234" .pio/build/simavr_nano/firmware.elf
```

**Note:** RS485, XBee, and RFM69 peripherals have no simavr models and are disabled in the `simavr_nano` build via `-DDISABLE_RS485 -DDISABLE_XBEE`. ESP32 targets are not covered by Track 3.

Configuration
-------------

The modules are configured via a JSON file, examples of which can be found in (see [examples](python/configs)).  To view and upload a modules configuration, load the HMTLPythonConfig sketch on the module.  The HMTLConfig.py tool can then be used to view the current configuration and load new configurations onto a module.

Networking
----------

HMTL Module Code
----------------

Wiring
------

## 4-Pin XLR

Pin | Use      | 4-wire color
--- | -------- | -------
1   | GND      | White
2   | Data 1/A | Black
3   | Data 2/B | Green
4   | VCC (12V)| Red    

Future work
-----------
