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
#   make packed-access          — -Werror=address-of-packed-member over the real .cpp
#   make REQUIRE_TOOLCHAINS=1   — treat any missing cross-toolchain as an error
#   make REQUIRE_ANY_REAL=1     — require at least one of avr-g++ / xtensa
#
# Two axes, not one. Each toolchain is compiled BOTH with and without
# -DBIG_PIXELS, because that flag is the only configuration in which
# hmtl_program_color_t ever had the interior-padding bug: with the default
# PIXEL_ADDR_TYPE = uint8_t the struct is 5 bytes with range at 3 on every ABI,
# packed or not, so a default-flags-only sweep leaves that guard unable to fail.
# Verified by deleting the packed attribute in a scratch copy: default flags
# stay green, -DBIG_PIXELS goes red with two failures.
#
# A missing avr-g++ or xtensa-esp32-elf-g++ is reported and skipped by default,
# because this has to be runnable on a machine that has neither. It is never
# silent: the run ends with a COVERAGE line naming what ran and what did not, so
# a partial sweep cannot be mistaken for a full one. REQUIRE_TOOLCHAINS=1 makes
# any skip an error; REQUIRE_ANY_REAL=1 is the weaker, CI-friendly form — it
# only insists that the sweep touched at least one real embedded ABI, so
# "everything passed" can never mean "nothing but the host ran".

HMTL_ROOT  := $(CURDIR)/../..
LIBS       := $(HMTL_ROOT)/Libraries
STUBS      := $(HMTL_ROOT)/platformio/HMTL_Test/stubs
NATIVE_LIB := $(HMTL_ROOT)/platformio/HMTL_Test/lib/hmtl_sources
PIO_PKG    ?= $(HOME)/.platformio/packages

AVR_CXX    ?= $(PIO_PKG)/toolchain-atmelavr/bin/avr-g++
XTENSA_CXX ?= $(PIO_PKG)/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++
HOST_CXX   ?= g++

REQUIRE_TOOLCHAINS ?=
REQUIRE_ANY_REAL   ?=

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

PIXEL_VARIANTS := default BIG_PIXELS
COVERAGE_FILE  := .coverage-note

# The COVERAGE line is assembled by appending to a file that `host` truncates
# and `summary` reads. Under -j that ordering is not guaranteed and the line
# comes out empty or stale while the build still exits 0 — which defeats the one
# mechanism keeping a partial sweep from reading as a full one.
.NOTPARALLEL:

.PHONY: all host host-avr-proxy avr xtensa negative packed-access summary clean

all: host host-avr-proxy avr xtensa summary

# $(1) label, $(2) compiler + arch flags
define sweep
	@rm -f $(COVERAGE_FILE).$(1)
	@for v in $(PIXEL_VARIANTS); do \
	  case $$v in default) extra="" ;; *) extra="-D$$v" ;; esac; \
	  echo "--- $(1) [$$v] ---"; \
	  $(2) $(CXXFLAGS) $$extra layout_check.cpp || exit 1; \
	done
	@echo "$(1)" >> $(COVERAGE_FILE)
endef

# The host as-is: 32-bit-style alignment with a 64-bit long.
host:
	@rm -f $(COVERAGE_FILE)
	$(call sweep,host,$(HOST_CXX))

# -fpack-struct=1 as an AVR stand-in. Valid for these structs ONLY because none
# of them contains a `long` or a pointer any more: `unsigned long` is 8 bytes on
# an LP64 host but 4 on both AVR and xtensa, which is exactly why a host
# measurement of the OLD msg_time_sync_t (9 bytes) matched neither real target.
# Changing timestamp to uint32_t is what made this proxy trustworthy. Do not
# extend it to a struct with a long or pointer member without re-checking.
host-avr-proxy:
	$(call sweep,host-avr-proxy,$(HOST_CXX) -fpack-struct=1)

avr:
	@if [ -x "$(AVR_CXX)" ]; then \
	  $(MAKE) --no-print-directory _sweep LABEL=avr CXX_CMD="$(AVR_CXX) -mmcu=atmega328p"; \
	elif [ -n "$(REQUIRE_TOOLCHAINS)" ]; then \
	  echo "ERROR: avr-g++ not found at $(AVR_CXX) (REQUIRE_TOOLCHAINS is set)"; exit 1; \
	else \
	  echo "--- avr-g++ SKIPPED: not found at $(AVR_CXX) ---"; \
	fi

xtensa:
	@if [ -x "$(XTENSA_CXX)" ]; then \
	  $(MAKE) --no-print-directory _sweep LABEL=xtensa CXX_CMD="$(XTENSA_CXX)"; \
	elif [ -n "$(REQUIRE_TOOLCHAINS)" ]; then \
	  echo "ERROR: xtensa-esp32-elf-g++ not found at $(XTENSA_CXX) (REQUIRE_TOOLCHAINS is set)"; exit 1; \
	else \
	  echo "--- xtensa-esp32-elf-g++ SKIPPED: not found at $(XTENSA_CXX) ---"; \
	fi

.PHONY: _sweep
_sweep:
	$(call sweep,$(LABEL),$(CXX_CMD))

# Packing makes &packed_field a possibly-unaligned pointer, which is undefined
# behaviour and a real runtime trap on xtensa. layout_check.cpp only includes
# headers, so it cannot catch an existing address-of; this compiles the actual
# implementation TU with the diagnostic promoted to an error.
#
# Scope, stated so it is not over-read: hmtl_sources.cpp pulls HMTLPrograms.cpp
# and ProgramManager.cpp, which is where the newly-packed PROGRAM payloads are
# read and written. TimeSync.cpp is not in that TU and is not covered here; its
# four uses of msg_time_sync_t were checked by hand instead — all by value, none
# taking an address. Note the include order below is the NATIVE suite's (stubs
# ahead of Libraries), unlike the sweep above: hmtl_sources.cpp brings in the
# stub TimeSync class, which collides with the real one if Libraries wins.
packed-access:
	@echo "--- address-of-packed-member (host, -Werror) ---"
	@for v in $(PIXEL_VARIANTS); do \
	  case $$v in default) extra="" ;; *) extra="-D$$v" ;; esac; \
	  echo "    [$$v]"; \
	  $(HOST_CXX) -fsyntax-only -std=c++11 -Werror=address-of-packed-member \
	    -include Arduino.h -DDEBUG_LEVEL=5 -DDISABLE_RS485 -DDISABLE_XBEE -DDISABLE_MPR121 \
	    -I$(NATIVE_LIB) -I$(STUBS) -I$(LIBS)/HMTLMessaging -I$(LIBS)/HMTLTypes \
	    -I$(LIBS)/HMTLprotocol $$extra $(NATIVE_LIB)/hmtl_sources.cpp || exit 1; \
	done

# Say what was actually covered. "The sweep passed" and "the sweep passed on the
# only two ABIs available here" have to read differently.
summary:
	@echo "=== layout sweep COVERAGE: $$(tr '\n' ' ' < $(COVERAGE_FILE)) x [$(PIXEL_VARIANTS)] ==="
	@if ! grep -q '^avr$$' $(COVERAGE_FILE) || ! grep -q '^xtensa$$' $(COVERAGE_FILE); then \
	  echo "    (partial — a real toolchain was missing; set REQUIRE_TOOLCHAINS=1 to make that an error)"; \
	fi
	@if [ -n "$(REQUIRE_ANY_REAL)" ] && ! grep -qE '^(avr|xtensa)$$' $(COVERAGE_FILE); then \
	  echo "ERROR: no real embedded toolchain ran — the sweep proved nothing about AVR or xtensa"; \
	  echo "       (REQUIRE_ANY_REAL is set; install avr-g++ or xtensa-esp32-elf-g++)"; \
	  exit 1; \
	fi

negative:
	./negative_control.py

clean:
	rm -f $(COVERAGE_FILE) $(COVERAGE_FILE).*
