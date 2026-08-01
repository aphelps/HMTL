# Cross-ABI layout sweep for the HMTL wire headers.
#
# An ATMega328 module and an ESP32 module share one RS485 bus, so every struct
# that crosses it must have the same size and the same field offsets under both
# toolchains. HMTLWireFormat.h, HMTLPrograms.h and TimeSync.h carry that as
# static_asserts next to each struct; this directory is what makes sure those
# asserts are actually evaluated by the compilers that matter.
#
#   make                        — compile layout_check.cpp with every toolchain found
#   make negative               — prove the asserts can fail (negative_control.py)
#   make REQUIRE_TOOLCHAINS=1   — treat a missing cross-toolchain as an error
#
# A missing avr-g++ or xtensa-esp32-elf-g++ is reported and skipped by default,
# because this has to be runnable on a machine that has neither. It is never
# silent: the run ends with a COVERAGE line naming what ran and what did not, so
# a partial sweep cannot be mistaken for a full one. Set REQUIRE_TOOLCHAINS=1
# where full coverage is the point — that is the switch CI should use once the
# runner has both toolchains installed.

HMTL_ROOT  := $(CURDIR)/../..
LIBS       := $(HMTL_ROOT)/Libraries
STUBS      := $(HMTL_ROOT)/platformio/HMTL_Test/stubs
PIO_PKG    ?= $(HOME)/.platformio/packages

AVR_CXX    ?= $(PIO_PKG)/toolchain-atmelavr/bin/avr-g++
XTENSA_CXX ?= $(PIO_PKG)/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++
HOST_CXX   ?= g++

REQUIRE_TOOLCHAINS ?=

# -I$(LIBS)/TimeSync comes first deliberately: HMTL_Test/stubs also holds a
# TimeSync.h, and that stub never declared msg_time_sync_t at all, which is how
# the struct stayed unpacked and unchecked while everything around it was fixed.
INCLUDES := -I$(LIBS)/TimeSync -I$(LIBS)/HMTLprotocol -I$(LIBS)/HMTLMessaging \
            -I$(LIBS)/HMTLTypes -I$(STUBS)

# DEBUG_LEVEL=0 keeps the debug stubs out of the way; the DISABLE_* flags match
# what the module builds use. -Wall stays on so the documented CRGB
# packed-attribute warnings remain visible rather than hidden by this harness.
CXXFLAGS := -fsyntax-only -std=c++11 -Wall -include Arduino.h -DDEBUG_LEVEL=0 \
            -DDISABLE_RS485 -DDISABLE_XBEE -DDISABLE_MPR121 $(INCLUDES)

COVERAGE_FILE := .coverage-note

.PHONY: all host host-avr-proxy avr xtensa negative summary clean

all: host host-avr-proxy avr xtensa summary

# The host as-is: 32-bit-style alignment with a 64-bit long.
host:
	@echo "--- host ($(HOST_CXX)) ---"
	@rm -f $(COVERAGE_FILE)
	$(HOST_CXX) $(CXXFLAGS) layout_check.cpp
	@echo "host" >> $(COVERAGE_FILE)

# -fpack-struct=1 as an AVR stand-in. Valid for these structs ONLY because none
# of them contains a `long` or a pointer any more: `unsigned long` is 8 bytes on
# an LP64 host but 4 on both AVR and xtensa, which is exactly why a host
# measurement of the OLD msg_time_sync_t (9 bytes) matched neither real target.
# Changing timestamp to uint32_t is what made this proxy trustworthy. Do not
# extend it to a struct with a long or pointer member without re-checking.
host-avr-proxy:
	@echo "--- host, -fpack-struct=1 (AVR proxy) ---"
	$(HOST_CXX) $(CXXFLAGS) -fpack-struct=1 layout_check.cpp
	@echo "host-avr-proxy" >> $(COVERAGE_FILE)

avr:
	@if [ -x "$(AVR_CXX)" ]; then \
	  echo "--- avr-g++ ---"; \
	  "$(AVR_CXX)" -mmcu=atmega328p $(CXXFLAGS) layout_check.cpp && echo "avr" >> $(COVERAGE_FILE); \
	elif [ -n "$(REQUIRE_TOOLCHAINS)" ]; then \
	  echo "ERROR: avr-g++ not found at $(AVR_CXX) (REQUIRE_TOOLCHAINS is set)"; exit 1; \
	else \
	  echo "--- avr-g++ SKIPPED: not found at $(AVR_CXX) ---"; \
	fi

xtensa:
	@if [ -x "$(XTENSA_CXX)" ]; then \
	  echo "--- xtensa-esp32-elf-g++ ---"; \
	  "$(XTENSA_CXX)" $(CXXFLAGS) layout_check.cpp && echo "xtensa" >> $(COVERAGE_FILE); \
	elif [ -n "$(REQUIRE_TOOLCHAINS)" ]; then \
	  echo "ERROR: xtensa-esp32-elf-g++ not found at $(XTENSA_CXX) (REQUIRE_TOOLCHAINS is set)"; exit 1; \
	else \
	  echo "--- xtensa-esp32-elf-g++ SKIPPED: not found at $(XTENSA_CXX) ---"; \
	fi

# Say what was actually covered. "The sweep passed" and "the sweep passed on the
# only two ABIs available here" have to read differently.
summary:
	@echo "=== layout sweep COVERAGE: $$(tr '\n' ' ' < $(COVERAGE_FILE)) ==="
	@if ! grep -q '^avr$$' $(COVERAGE_FILE) || ! grep -q '^xtensa$$' $(COVERAGE_FILE); then \
	  echo "    (partial — a real toolchain was missing; set REQUIRE_TOOLCHAINS=1 to make that an error)"; \
	fi

negative:
	./negative_control.py

clean:
	rm -f $(COVERAGE_FILE)
