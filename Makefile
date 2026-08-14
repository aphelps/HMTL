# HMTL Makefile
#
# Targets:
#   make all              — build + test + coverage
#   make build            — build HMTL_Module (default env: nano)
#   make build-all        — build all platformio projects
#   make install-dev      — editable pip install so CLI tools use the current branch
#   make test             — run all tests (Tracks 1-4)
#   make test-python      — Track 1: Python emulator + unit tests
#   make test-native      — Track 2: C++ firmware logic, PlatformIO native
#   make test-simavr      — Track 3: AVR firmware build check (avr-gcc)
#   make test-layout      — Track 4: cross-ABI wire layout sweep (host/AVR/xtensa)
#   make test-layout-negative — prove every layout assert fails when broken
#   make coverage         — run Python + C++ coverage and print summaries
#   make coverage-python  — Python coverage via pytest-cov
#   make coverage-native  — C++ coverage via LLVM instrumentation + llvm-cov

PYTHON     ?= $(HOME)/.platformio/penv/bin/python3
PIO        ?= pio
PYTEST     ?= pytest
ENV        ?= nano

PYTHON_DIR := python
NATIVE_DIR := platformio/HMTL_Test
MODULE_DIR := platformio/HMTL_Module
LAYOUT_DIR := tests/layout

COVERAGE_PROFRAW := /tmp/hmtl_coverage
COVERAGE_PROFDATA := /tmp/hmtl_coverage.profdata
COVERAGE_BINARY  := $(NATIVE_DIR)/.pio/build/native_coverage/program

.PHONY: all build build-all install-dev test test-python test-python-all test-native test-simavr test-layout test-layout-negative coverage coverage-python coverage-native

all: build test coverage

build:
	@echo "=== Building HMTL_Module ($(ENV)) ==="
	cd $(MODULE_DIR) && $(PIO) run -e $(ENV)

install-dev:
	@echo "=== Installing HMTL Python package in editable mode ==="
	pip install -e $(PYTHON_DIR)/

build-all:
	@echo "=== Building all HMTL platformio projects ==="
	cd platformio/HMTL_Module       && $(PIO) run -e nano
	cd platformio/HMTL_Bringup      && $(PIO) run -e nano
	cd platformio/HMTLPythonConfig  && $(PIO) run -e nano
	cd platformio/HMTL_Command_CLI  && $(PIO) run -e nano
	cd platformio/PooferTest        && $(PIO) run -e nano
	cd platformio/TimeSyncExample   && $(PIO) run -e nano

# test-layout-negative is in the default run, not an optional extra: it takes
# ~60 s and it is the only thing standing between "the layout guard passed" and
# "the layout guard cannot fail", which this subsystem has produced before —
# including once in this very change, where the colour struct's packing guard
# could not fail until the sweep started compiling -DBIG_PIXELS as well.
test: test-python test-native test-simavr test-layout test-layout-negative

test-python:
	@echo "=== Track 1: Python emulator tests ==="
	cd $(PYTHON_DIR) && $(PYTEST) hmtl/tests/test_emulator.py -v

# Run the full Python test suite (includes pre-existing failures in test_CircularBuffer)
test-python-all:
	cd $(PYTHON_DIR) && $(PYTEST) hmtl/tests/ -v

test-native:
	@echo "=== Track 2: C++ native tests ==="
	cd $(NATIVE_DIR) && $(PIO) test -e native

test-simavr:
	@echo "=== Track 3: AVR firmware build check (avr-gcc) ==="
	cd $(MODULE_DIR) && $(PIO) run -e simavr_nano

# The wire structs in HMTLWireFormat.h, HMTLPrograms.h and TimeSync.h carry
# static_asserts on their size and field offsets, and the whole point of those
# asserts is that an ATMega328 and an ESP32 agree byte for byte.
#
# The firmware envs above cannot be that check, which is worth stating plainly
# because it looks like they can: HMTL_Module's platformio.ini resolves its
# libraries from a machine-local Arduino directory, not from this repo, so
# test-simavr compiles whatever copy of HMTLPrograms.h happens to be installed
# there. Verified rather than assumed - putting a deliberately impossible
# static_assert in Libraries/HMTLMessaging/HMTLPrograms.h leaves test-simavr
# green. (Repointing the module builds at Libraries/ is a separate job: they
# also need FastLED, Xbee, RFM69 and friends, which this repo does not vendor.)
#
# tests/layout/ compiles the repo's real headers directly with every toolchain
# available - host, host -fpack-struct=1, avr-g++ and xtensa-esp32-elf-g++ - and
# `make -C tests/layout negative` proves each assert fails when broken.
test-layout:
	@echo "=== Track 4: cross-ABI wire layout sweep ==="
	$(MAKE) -C $(LAYOUT_DIR)
	$(MAKE) -C $(LAYOUT_DIR) packed-access

test-layout-negative:
	@echo "=== Negative control: every layout assert must fail when broken ==="
	$(MAKE) -C $(LAYOUT_DIR) negative

coverage-python:
	@echo "=== Python coverage ==="
	cd $(PYTHON_DIR) && $(PYTHON) -m pytest hmtl/tests/test_emulator.py --cov=hmtl --cov-report=term-missing

coverage-native:
	@echo "=== C++ native coverage ==="
	rm -f $(COVERAGE_PROFRAW)-*.profraw
	cd $(NATIVE_DIR) && LLVM_PROFILE_FILE="$(COVERAGE_PROFRAW)-%p.profraw" $(PIO) test -e native_coverage
	xcrun llvm-profdata merge -sparse $(COVERAGE_PROFRAW)-*.profraw -o $(COVERAGE_PROFDATA)
	@echo "--- C++ Coverage Summary ---"
	xcrun llvm-cov report $(COVERAGE_BINARY) \
	    -instr-profile=$(COVERAGE_PROFDATA) \
	    -ignore-filename-regex='stubs/|test/|unity'

coverage: coverage-python coverage-native
	@echo ""
	@echo "============================================================"
	@echo "=== Combined Coverage Summary: All Files               ==="
	@echo "============================================================"
	@echo "--- Python ---"
	cd $(PYTHON_DIR) && $(PYTHON) -m coverage report
	@echo "--- C++ ---"
	xcrun llvm-cov report $(COVERAGE_BINARY) \
	    -instr-profile=$(COVERAGE_PROFDATA) \
	    -ignore-filename-regex='stubs/|test/|unity'
