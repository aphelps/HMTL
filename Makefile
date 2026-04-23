# HMTL Makefile
#
# Targets:
#   make all           — build + test
#   make build         — build HMTL_Module (default env: nano)
#   make build-all     — build all platformio projects
#   make install-dev   — editable pip install so CLI tools use the current branch
#   make test          — run all tests (Track 1 + Track 2 + Track 3)
#   make test-python   — Track 1: Python emulator + unit tests
#   make test-native   — Track 2: C++ firmware logic, PlatformIO native
#   make test-simavr   — Track 3: AVR firmware build check (avr-gcc)

PYTHON     ?= python3
PIO        ?= pio
PYTEST     ?= pytest
ENV        ?= nano

PYTHON_DIR := python
NATIVE_DIR := platformio/HMTL_Test
MODULE_DIR := platformio/HMTL_Module

.PHONY: all build build-all install-dev test test-python test-python-all test-native test-simavr

all: build test

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

test: test-python test-native test-simavr

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
