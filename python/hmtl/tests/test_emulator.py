"""Automated integration tests for the HMTL Python emulator.

Each test uses EmulatorProcess to launch the emulator as a subprocess in
virtual-time mode, send HMTL messages via the control port, advance virtual
time, and assert on JSON state output — no physical device required.

Run:
    cd python
    pytest hmtl/tests/test_emulator.py -v
"""

import json
import os
import socket
import struct
import subprocess
import sys
import time
import threading

import pytest

import hmtl.HMTLprotocol as HMTLprotocol

# Path to test config relative to this file
_HERE = os.path.dirname(os.path.abspath(__file__))
_TEST_CONFIG = os.path.join(_HERE, "test_config.json")
_EMULATOR_SCRIPT = os.path.join(_HERE, "..", "..", "bin", "HMTLEmulator")

# Address used by test_config.json
_TEST_ADDRESS = 1


class EmulatorProcess:
    """Context manager that runs HMTLEmulator as a subprocess in virtual-time mode.

    All communication goes through the JSON control port — no dependency on
    real TCP framing or HMTLClient authentication.
    """

    def __init__(self, config=None, port=None, control_port=None):
        self.config = config or _TEST_CONFIG
        self.port = port or _find_free_port()
        self.control_port = control_port or _find_free_port()
        self._proc = None
        self._ctrl_sock = None
        self._ctrl_file = None

    def __enter__(self):
        self._proc = subprocess.Popen(
            [sys.executable, "-u", _EMULATOR_SCRIPT,
             self.config,
             "--port", str(self.port),
             "--control-port", str(self.control_port),
             "--virtual-time"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        # Wait for control server to be ready
        deadline = time.time() + 5.0
        while time.time() < deadline:
            try:
                self._ctrl_sock = socket.create_connection(
                    ("localhost", self.control_port), timeout=0.5)
                break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.05)
        else:
            self._proc.kill()
            raise RuntimeError("Emulator did not start within 5s")
        self._ctrl_file = self._ctrl_sock.makefile("r")
        return self

    def __exit__(self, *args):
        if self._ctrl_sock:
            try:
                self._ctrl_sock.close()
            except Exception:
                pass
        if self._proc:
            self._proc.terminate()
            try:
                self._proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self._proc.kill()

    # ------------------------------------------------------------------
    # Low-level control channel
    # ------------------------------------------------------------------

    def _ctrl_send(self, cmd):
        line = json.dumps(cmd) + "\n"
        self._ctrl_sock.sendall(line.encode())
        resp_line = self._ctrl_file.readline()
        if not resp_line:
            raise RuntimeError("Control connection closed unexpectedly")
        return json.loads(resp_line.strip())

    # ------------------------------------------------------------------
    # Public API mirroring HMTLClient capabilities
    # ------------------------------------------------------------------

    def send_hmtl(self, msg_bytes):
        """Send raw HMTL message bytes (no TCPSocketHeader needed)."""
        resp = self._ctrl_send({"cmd": "send_hmtl", "data": msg_bytes.hex()})
        assert resp.get("ok"), "send_hmtl failed: %s" % resp

    def send_rgb(self, output, r, g, b,
                 address=_TEST_ADDRESS):
        msg = HMTLprotocol.get_rgb_msg(address, output, r, g, b)
        self.send_hmtl(msg)

    def send_value(self, output, value, address=_TEST_ADDRESS):
        msg = HMTLprotocol.get_value_msg(address, output, value)
        self.send_hmtl(msg)

    def send_blink(self, output, on_period, on_value, off_period, off_value,
                   address=_TEST_ADDRESS):
        msg = HMTLprotocol.get_program_blink_msg(
            address, output, on_period, on_value, off_period, off_value)
        self.send_hmtl(msg)

    def send_none(self, output, address=_TEST_ADDRESS):
        msg = HMTLprotocol.get_program_none_msg(address, output)
        self.send_hmtl(msg)

    def send_sequence(self, steps, address=_TEST_ADDRESS):
        msg = HMTLprotocol.get_program_sequence_msg(address, steps)
        self.send_hmtl(msg)

    def advance(self, ms):
        """Advance virtual clock by ms milliseconds."""
        resp = self._ctrl_send({"cmd": "advance", "ms": ms})
        assert resp.get("ok"), "advance failed: %s" % resp

    def get_state(self):
        """Return current emulator state dict."""
        resp = self._ctrl_send({"cmd": "state"})
        assert resp.get("ok"), "state failed: %s" % resp
        return resp

    def reset(self):
        resp = self._ctrl_send({"cmd": "reset"})
        assert resp.get("ok"), "reset failed: %s" % resp


def _find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("", 0))
        return s.getsockname()[1]


# ===========================================================================
# Tests
# ===========================================================================

def test_emulator_starts_and_returns_state():
    """Emulator starts, returns correct output count and types."""
    with EmulatorProcess() as emu:
        state = emu.get_state()
    assert state["address"] == _TEST_ADDRESS
    outputs = state["outputs"]
    assert len(outputs) == 3
    assert outputs[0]["type"] == "value"
    assert outputs[1]["type"] == "value"
    assert outputs[2]["type"] == "rgb"


def test_set_rgb_output():
    """RGB output message sets the correct values."""
    with EmulatorProcess() as emu:
        emu.send_rgb(output=2, r=100, g=150, b=200)
        state = emu.get_state()
    assert state["outputs"][2]["values"] == [100, 150, 200]


def test_set_value_output():
    """Value output message sets the correct value."""
    with EmulatorProcess() as emu:
        emu.send_value(output=0, value=255)
        state = emu.get_state()
    assert state["outputs"][0]["value"] == 255


def test_blink_program_turns_on_immediately():
    """Blink program sets output to on_value on first tick."""
    with EmulatorProcess() as emu:
        emu.send_blink(output=2,
                       on_period=500, on_value=(255, 0, 0),
                       off_period=500, off_value=(0, 0, 0))
        emu.advance(0)  # tick at t=0 initializes the program
        state = emu.get_state()
    assert state["outputs"][2]["values"] == [255, 0, 0]


def test_blink_program_toggles_off():
    """Blink program toggles to off_value after on_period ms."""
    with EmulatorProcess() as emu:
        emu.send_blink(output=2,
                       on_period=200, on_value=(255, 0, 0),
                       off_period=200, off_value=(0, 0, 0))
        emu.advance(0)    # initialize
        emu.advance(250)  # past on_period=200 → should toggle off
        state = emu.get_state()
    assert state["outputs"][2]["values"] == [0, 0, 0]


def test_blink_program_toggles_on_again():
    """Blink program toggles back to on_value after off_period ms."""
    with EmulatorProcess() as emu:
        emu.send_blink(output=2,
                       on_period=200, on_value=(255, 0, 0),
                       off_period=200, off_value=(0, 0, 0))
        emu.advance(0)    # initialize: on
        emu.advance(250)  # t=250: toggle off (on_period=200 passed)
        emu.advance(250)  # t=500: toggle on again (off_period=200 passed at t=400)
        state = emu.get_state()
    assert state["outputs"][2]["values"] == [255, 0, 0]


def test_cancel_program():
    """Sending PROGRAM_NONE cancels a running program."""
    with EmulatorProcess() as emu:
        emu.send_blink(output=2,
                       on_period=100, on_value=(255, 0, 0),
                       off_period=100, off_value=(0, 0, 0))
        emu.advance(0)
        emu.send_none(output=2)
        emu.advance(200)  # would have toggled if program still ran
        state = emu.get_state()
    # Output should remain at whatever it was when cancelled — not toggled by program
    assert state["outputs"][2]["values"] == [255, 0, 0]


def test_sequence_program_starts_step_0():
    """Sequence program activates first step on first tick."""
    with EmulatorProcess() as emu:
        # steps: output 0 for 500ms at value 200, then output 1 for 300ms at value 100
        emu.send_sequence(steps=[(0, 500, 200), (1, 300, 100)])
        emu.advance(0)  # initialize
        state = emu.get_state()
    # output 0 should be on with value 200 (set_rgb(200,200,200) → value output uses r)
    assert state["outputs"][0]["value"] == 200
    # output 1 should still be off
    assert state["outputs"][1]["value"] == 0


def test_sequence_program_advances_to_next_step():
    """Sequence program moves to next step after duration expires."""
    with EmulatorProcess() as emu:
        emu.send_sequence(steps=[(0, 300, 200), (1, 400, 100)])
        emu.advance(0)    # init: step 0 on, next_change=300
        emu.advance(350)  # t=350: step 0 expires → step 1 on
        state = emu.get_state()
    assert state["outputs"][0]["value"] == 0    # step 0 off
    assert state["outputs"][1]["value"] == 100  # step 1 on


def test_sequence_program_wraps_around():
    """Sequence program wraps from last step back to step 0."""
    with EmulatorProcess() as emu:
        emu.send_sequence(steps=[(0, 100, 200), (1, 100, 100)])
        emu.advance(0)    # init: step 0
        emu.advance(150)  # t=150: step 1
        emu.advance(150)  # t=300: wrap to step 0
        state = emu.get_state()
    assert state["outputs"][0]["value"] == 200
    assert state["outputs"][1]["value"] == 0


def test_broadcast_address_reaches_module():
    """Messages with BROADCAST address are processed by the emulator."""
    with EmulatorProcess() as emu:
        # Use BROADCAST address (65535) instead of module address
        msg = HMTLprotocol.get_rgb_msg(HMTLprotocol.BROADCAST, 2, 10, 20, 30)
        emu.send_hmtl(msg)
        state = emu.get_state()
    assert state["outputs"][2]["values"] == [10, 20, 30]


def test_wrong_address_is_ignored():
    """Messages addressed to a different module are ignored."""
    with EmulatorProcess() as emu:
        # Address 99 != module address 1
        msg = HMTLprotocol.get_rgb_msg(99, 2, 10, 20, 30)
        emu.send_hmtl(msg)
        state = emu.get_state()
    assert state["outputs"][2]["values"] == [0, 0, 0]  # unchanged


def test_reset_clears_programs_and_outputs():
    """reset() clears all program state and output values."""
    with EmulatorProcess() as emu:
        emu.send_rgb(output=2, r=100, g=100, b=100)
        emu.send_blink(output=2,
                       on_period=100, on_value=(255, 0, 0),
                       off_period=100, off_value=(0, 0, 0))
        emu.advance(0)
        emu.reset()
        emu.advance(200)  # program cleared — should not run
        state = emu.get_state()
    assert state["outputs"][2]["values"] == [0, 0, 0]
