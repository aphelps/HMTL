# HMTL top-level Makefile
#
# Targets:
#   make test          — run all tests (Track 1 + Track 2)
#   make test-python   — Track 1: Python emulator + unit tests
#   make test-native   — Track 2: C++ firmware logic, PlatformIO native
#   make test-simavr   — Track 3: Full AVR emulation (requires: brew install simavr)

PYTHON     ?= python3
PIO        ?= pio
PYTEST     ?= pytest

PYTHON_DIR := python
NATIVE_DIR := platformio/HMTL_Test
SIMAVR_DIR := platformio/HMTL_Module

.PHONY: test test-python test-python-all test-native test-simavr

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
	cd $(SIMAVR_DIR) && $(PIO) run -e simavr_nano
	@echo "Note: to run tests inside simavr, install it first: brew install simavr"
