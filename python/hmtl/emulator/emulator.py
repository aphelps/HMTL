"""HMTL software emulator.

Accepts the same TCP messages as a real hardware HMTL module via
Python's multiprocessing.connection protocol (for HMTLClient compatibility).

Two modes:
  Real-time:    uses wall-clock time; programs tick in a background thread.
  Virtual-time: clock starts at 0 and advances only via the control port;
                used for deterministic automated tests.

Control port (virtual-time mode only, JSON lines over raw TCP):
  {"cmd": "send_hmtl", "data": "<hex bytes>"}  -> {"ok": true}
  {"cmd": "advance",   "ms": N}               -> {"ok": true}
  {"cmd": "state"}                             -> {"ok": true, "address": N, "outputs": [...]}
  {"cmd": "reset"}                             -> {"ok": true}
"""

import json
import socket
import struct
import threading
import time

from multiprocessing.connection import Listener

import hmtl.HMTLprotocol as HMTLprotocol
from hmtl.emulator.outputs import make_output
from hmtl.emulator.programs import (
    BlinkProgram, TimedChangeProgram, FadeProgram, SequenceProgram
)

# Message header constants (matching HMTLprotocol.py)
_MSG_HDR_FMT = HMTLprotocol.MSG_HDR_FMT  # "<BBBBBBH"
_MSG_HDR_LEN = 8
_OUT_HDR_FMT = "<BB"
_OUT_HDR_LEN = 2
_BROADCAST   = HMTLprotocol.BROADCAST

# Output type codes (must match CONFIG_TYPES in constants.py)
_OTYPE_VALUE   = 0x1
_OTYPE_RGB     = 0x2
_OTYPE_PROGRAM = 0x3

# Special output index sentinels
_NO_OUTPUT  = 0xFF
_ALL_OUTPUT = 0xFE


class HMTLEmulator:
    def __init__(self, config, address=None, verbose=False):
        if isinstance(config, str):
            with open(config) as f:
                config = json.load(f)

        self.address = address if address is not None else config["header"]["address"]
        self.verbose = verbose

        self.outputs  = [make_output(o) for o in config["outputs"]]
        self._programs = [None] * len(self.outputs)  # per-output programs
        self._no_output_program = None               # for HMTL_NO_OUTPUT programs

        self._lock = threading.Lock()
        self._virtual_time = False
        self._virtual_ms = 0

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def start(self, port=HMTLprotocol.HMTL_PORT, control_port=None,
              virtual_time=False):
        """Start the emulator servers.  Returns self for chaining."""
        self._virtual_time = virtual_time

        if virtual_time and control_port:
            # Control server runs synchronously in the main caller thread if
            # invoked from a subprocess; here we start it in a background thread
            # so the caller can use it via connect().
            t = threading.Thread(
                target=self._control_server_thread, args=(control_port,),
                daemon=True)
            t.start()
        else:
            # Real-time mode: start HMTL listener + tick loop
            t = threading.Thread(
                target=self._hmtl_server_thread, args=(port,), daemon=True)
            t.start()
            t2 = threading.Thread(target=self._tick_loop, daemon=True)
            t2.start()

        return self

    def advance(self, ms):
        """Advance virtual clock by ms milliseconds and run all program ticks."""
        with self._lock:
            self._virtual_ms += ms
            self._run_programs(self._virtual_ms)

    def get_state(self):
        """Return a dict snapshot of all output states."""
        with self._lock:
            return {
                "address": self.address,
                "outputs": [o.state() for o in self.outputs],
            }

    def reset(self):
        """Reset all outputs and programs to initial state."""
        with self._lock:
            self._virtual_ms = 0
            self._programs = [None] * len(self.outputs)
            self._no_output_program = None
            for o in self.outputs:
                o.reset()

    # ------------------------------------------------------------------
    # Message handling (called from either real-time or virtual-time path)
    # ------------------------------------------------------------------

    def handle_bytes(self, data):
        """Parse raw HMTL bytes (without TCPSocketHeader) and update state."""
        if len(data) < _MSG_HDR_LEN:
            return

        startcode, crc, version, length, mtype, flags, address = \
            struct.unpack_from(_MSG_HDR_FMT, data)

        if startcode != 0xFC:
            return
        if address != _BROADCAST and address != self.address:
            return

        if mtype == HMTLprotocol.MSG_TYPE_OUTPUT:
            with self._lock:
                self._dispatch_output(data[_MSG_HDR_LEN:])

    def _dispatch_output(self, data):
        """Called with lock held. Parse output_hdr_t + payload."""
        if len(data) < _OUT_HDR_LEN:
            return
        outputtype, output_idx = struct.unpack_from(_OUT_HDR_FMT, data)
        payload = data[_OUT_HDR_LEN:]

        if outputtype == _OTYPE_VALUE:
            if len(payload) >= 2:
                value = struct.unpack_from("<H", payload)[0] & 0x1FFF
                self._set_output_value(output_idx, value)

        elif outputtype == _OTYPE_RGB:
            if len(payload) >= 3:
                r, g, b = struct.unpack_from("BBB", payload)
                self._set_output_rgb(output_idx, r, g, b)

        elif outputtype == _OTYPE_PROGRAM:
            if len(payload) >= 1:
                program_type = struct.unpack_from("B", payload)[0]
                program_data = payload[1:]
                self._install_program(output_idx, program_type, program_data)

    # ------------------------------------------------------------------
    # State mutation helpers (called with lock held)
    # ------------------------------------------------------------------

    def _set_output_value(self, output_idx, value):
        if output_idx == _ALL_OUTPUT:
            for o in self.outputs:
                o.set_value(value)
        elif 0 <= output_idx < len(self.outputs):
            self.outputs[output_idx].set_value(value)

    def _set_output_rgb(self, output_idx, r, g, b):
        if output_idx == _ALL_OUTPUT:
            for o in self.outputs:
                o.set_rgb(r, g, b)
        elif 0 <= output_idx < len(self.outputs):
            self.outputs[output_idx].set_rgb(r, g, b)

    def _install_program(self, output_idx, program_type, program_data):
        if program_type == 0x00:  # HMTL_PROGRAM_NONE — cancel
            if output_idx == _NO_OUTPUT:
                self._no_output_program = None
            elif output_idx == _ALL_OUTPUT:
                self._programs = [None] * len(self.outputs)
            elif 0 <= output_idx < len(self.outputs):
                self._programs[output_idx] = None
            return

        program = self._parse_program(program_type, program_data)
        if program is None:
            return

        if output_idx == _NO_OUTPUT:
            self._no_output_program = program
        elif output_idx == _ALL_OUTPUT:
            for i in range(len(self.outputs)):
                self._programs[i] = program
        elif 0 <= output_idx < len(self.outputs):
            self._programs[output_idx] = program

    def _parse_program(self, program_type, data):
        """Parse program payload bytes into a Program object."""
        try:
            if program_type == 0x01:  # BLINK: <HBBBHBBB> + padding
                on_period, on_r, on_g, on_b, off_period, off_r, off_g, off_b = \
                    struct.unpack_from("<HBBBHBBB", data)
                return BlinkProgram(on_period, (on_r, on_g, on_b),
                                    off_period, (off_r, off_g, off_b))

            elif program_type == 0x02:  # TIMED_CHANGE: <LBBBBBB> + padding
                period, sr, sg, sb, er, eg, eb = struct.unpack_from("<LBBBBBB", data)
                return TimedChangeProgram(period, (sr, sg, sb), (er, eg, eb))

            elif program_type == 0x05:  # FADE: <LBBBBBBB> + padding
                period, sr, sg, sb, er, eg, eb, flags = \
                    struct.unpack_from("<LBBBBBBB", data)
                return FadeProgram(period, (sr, sg, sb), (er, eg, eb), flags)

            elif program_type == 0x09:  # SEQUENCE: 8xB outputs + 8xH durations + 8xB values
                N = 8
                outputs   = list(struct.unpack_from("<%dB" % N, data, 0))
                durations = list(struct.unpack_from("<%dH" % N, data, N))
                values    = list(struct.unpack_from("<%dB" % N, data, N + N * 2))
                steps = []
                for i in range(N):
                    if outputs[i] == 0xFF:
                        break
                    steps.append((outputs[i], durations[i], values[i]))
                if not steps:
                    return None
                return SequenceProgram(steps)

        except struct.error:
            if self.verbose:
                print("emulator: failed to parse program type 0x%02x" % program_type)

        return None

    # ------------------------------------------------------------------
    # Program tick
    # ------------------------------------------------------------------

    def _run_programs(self, now_ms):
        """Execute one tick of all active programs.  Must be called with lock held."""
        for i, prog in enumerate(self._programs):
            if prog is not None:
                prog.tick(now_ms, self.outputs[i])

        if self._no_output_program is not None:
            self._no_output_program.tick(now_ms, self.outputs)

    # ------------------------------------------------------------------
    # Real-time servers
    # ------------------------------------------------------------------

    def _hmtl_server_thread(self, port):
        listener = Listener(('localhost', port), authkey=None)
        while True:
            try:
                conn = listener.accept()
                t = threading.Thread(
                    target=self._hmtl_client_handler, args=(conn,), daemon=True)
                t.start()
            except Exception as e:
                if self.verbose:
                    print("HMTL server error: %s" % e)

    def _hmtl_client_handler(self, conn):
        try:
            while True:
                data = conn.recv()
                if isinstance(data, (bytes, bytearray)):
                    self.handle_bytes(data)
        except EOFError:
            pass
        except Exception as e:
            if self.verbose:
                print("HMTL client handler error: %s" % e)

    def _tick_loop(self):
        while True:
            time.sleep(0.01)
            with self._lock:
                self._run_programs(int(time.time() * 1000))

    # ------------------------------------------------------------------
    # Virtual-time control server (raw TCP + JSON lines)
    # ------------------------------------------------------------------

    def _control_server_thread(self, port):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            s.bind(('localhost', port))
            s.listen(5)
            while True:
                try:
                    conn, _ = s.accept()
                    t = threading.Thread(
                        target=self._control_client_handler, args=(conn,),
                        daemon=True)
                    t.start()
                except Exception as e:
                    if self.verbose:
                        print("Control server error: %s" % e)

    def _control_client_handler(self, conn):
        try:
            f = conn.makefile('r')
            while True:
                line = f.readline()
                if not line:
                    break
                try:
                    cmd = json.loads(line.strip())
                    resp = self._handle_control_cmd(cmd)
                except Exception as e:
                    resp = {"ok": False, "error": str(e)}
                try:
                    conn.sendall((json.dumps(resp) + "\n").encode())
                except Exception:
                    break
        except Exception:
            pass
        finally:
            conn.close()

    def _handle_control_cmd(self, cmd):
        name = cmd.get("cmd")

        if name == "send_hmtl":
            raw = bytes.fromhex(cmd["data"])
            self.handle_bytes(raw)
            return {"ok": True}

        elif name == "advance":
            self.advance(int(cmd["ms"]))
            return {"ok": True}

        elif name == "state":
            state = self.get_state()
            return {"ok": True, **state}

        elif name == "reset":
            self.reset()
            return {"ok": True}

        return {"ok": False, "error": "unknown command: %s" % name}
