"""Host-side tests for connecting to a device over serial.

These cover the three things that made `HMTLCommandServer -d <esp32>` unusable:
the ready handshake could not survive a device that does not reset when the
port is opened, the reader could be desynced by the ESP32's boot-ROM noise, and
the baud rate could be silently wrong.

Everything here runs without hardware.  The timing tests use a virtual clock,
so they are instant and deterministic; the DTR/HUPCL tests use a pty, so what
they assert about pyserial is what this platform actually does rather than what
the documentation implies.

Run:
    cd python
    pytest hmtl/tests/test_serial_connect.py -v
"""

import inspect
import os
import pty
import sys
import termios

import pytest

import hmtl.HMTLprotocol as HMTLprotocol
import hmtl.HMTLSerial as HMTLSerial_module
from hmtl.HMTLSerial import HMTLSerial, HMTLReadyTimeout
from hmtl.InputBuffer import InputBuffer, InputItem
from hmtl.SerialBuffer import SerialBuffer, _disable_hupcl


READY = HMTLprotocol.HMTL_CONFIG_READY


# ---------------------------------------------------------------------------
# Test doubles
# ---------------------------------------------------------------------------

class _Clock:
    """A virtual clock, so a 12-second timeout takes no wall time to test."""

    def __init__(self, start=1000.0):
        self.now = start

    def time(self):
        return self.now

    def advance(self, seconds):
        self.now += seconds


class _ScriptedBuffer:
    """Stands in for a SerialBuffer, replaying a timed script of items.

    `script` is a list of `(seconds_after_connect, payload)`.  `get(wait)`
    models `CircularBuffer.get()` -> `queue.get(block, timeout)`: it returns as
    soon as an item is due, and otherwise consumes the whole timeout *plus a
    hair*.  That last part is not a fudge -- `Queue.get` blocks for at least the
    timeout and never less, so ten one-second polls always land past the
    ten-second mark.  It is exactly why the old `MAX_READY_WAIT = 10` gave up
    before a `ready` at 10.5 s could be read.
    """

    POLL_OVERHEAD = 0.001

    def __init__(self, clock, script, device="/dev/fake-usbserial", baud=115200):
        self.clock = clock
        self.script = sorted(script, key=lambda entry: entry[0])
        self.device = device
        self.baud = baud
        self.start_time = clock.time()
        self.started = False
        self.written = []

    def start(self):
        self.started = True

    def get(self, wait=None):
        deadline = self.clock.now + (3600 if wait is None else wait)

        if self.script:
            due, payload = self.script[0]
            arrival = self.start_time + due
            if arrival <= deadline:
                self.script.pop(0)
                self.clock.now = max(self.clock.now, arrival)
                return InputItem.from_data(payload, self.clock.now)

        self.clock.now = deadline + self.POLL_OVERHEAD
        return None

    def write(self, data):
        self.written.append(data)


@pytest.fixture
def clock(monkeypatch):
    """Install a virtual clock in place of HMTLSerial's `time` module."""
    c = _Clock()
    monkeypatch.setattr(HMTLSerial_module, "time", c)
    return c


def _connect(clock, script, **kwargs):
    """Connect for real: HMTLSerial.__init__ runs the handshake itself.

    Returns the scripted buffer so a test can assert the `ready` was actually
    consumed -- construction completing without HMTLReadyTimeout is the pass
    condition, and an empty script proves it was the `ready` that ended the
    wait rather than an early return.
    """
    buff = _ScriptedBuffer(clock, script, **kwargs)
    HMTLSerial(buff)
    return buff


# ---------------------------------------------------------------------------
# The ready handshake
# ---------------------------------------------------------------------------

def test_max_ready_wait_exceeds_the_firmware_resend_window():
    """The client must outlast the firmware's first possible resend.

    MessageHandler::serial_ready() will not resend `ready` until READY_THRESHOLD
    of serial silence, and then only every READY_RESEND_PERIOD.  A client that
    gives up at READY_THRESHOLD can therefore never be rescued by the resend --
    which is the only announcement it gets from a device that was not reset by
    opening the port.
    """
    assert (HMTLSerial.MAX_READY_WAIT >
            HMTLSerial.FIRMWARE_READY_THRESHOLD +
            HMTLSerial.FIRMWARE_READY_RESEND_PERIOD)


def test_fast_ready_is_not_discarded(clock):
    """Regression: a `ready` inside the first 0.5 s was thrown away.

    wait_for_ready() used to `continue` past everything received in the first
    half second, so a board that boots fast had its only timely announcement
    dropped -- and then had to wait for a resend that could not arrive in time.
    """
    buff = _connect(clock, [(0.1, READY)])

    assert buff.script == []


def test_ready_after_ten_and_a_half_seconds_is_accepted(clock):
    """The load-bearing case: fails against the pre-fix `MAX_READY_WAIT = 10`.

    A device that is not reset by opening the port says nothing until
    MessageHandler::serial_ready() decides to resend, which cannot happen before
    READY_THRESHOLD (10 s).  The old timeout expired on the poll that ended just
    past 10.000 s -- before this `ready` was readable.
    """
    buff = _connect(clock, [(10.5, READY)])

    assert buff.script == []


def test_ready_at_the_worst_case_resend_is_accepted(clock):
    """READY_THRESHOLD + READY_RESEND_PERIOD is the latest a resend can be due."""
    latest = (HMTLSerial.FIRMWARE_READY_THRESHOLD +
              HMTLSerial.FIRMWARE_READY_RESEND_PERIOD)
    buff = _connect(clock, [(latest, READY)])

    assert buff.script == []


def test_banner_lines_before_ready_are_skipped(clock):
    """Application output precedes `ready`; none of it should end the wait."""
    buff = _connect(clock, [
        (0.2, b"HMTL Fire Control v2"),
        (0.4, b"config: device 129"),
        (1.0, READY),
    ])

    assert buff.script == []


def test_timeout_names_the_device_the_baud_and_what_was_seen(clock):
    """A ready timeout must say enough to diagnose it without a second run."""
    script = [(t, b"\xff\xf0garbage") for t in (0.5, 1.5, 2.5)]

    with pytest.raises(HMTLReadyTimeout) as excinfo:
        _connect(clock, script, device="/dev/cu.usbserial-TEST", baud=57600)

    message = str(excinfo.value)
    assert "/dev/cu.usbserial-TEST" in message
    assert "57600" in message
    assert "ready" in message
    # The bytes actually seen on the wire, so a wrong baud is obvious.
    assert "garbage" in message or "raw" in message


def test_timeout_when_nothing_at_all_arrives(clock):
    with pytest.raises(HMTLReadyTimeout) as excinfo:
        _connect(clock, [], device="/dev/cu.usbserial-TEST", baud=115200)

    message = str(excinfo.value)
    assert "/dev/cu.usbserial-TEST" in message
    assert "115200" in message
    assert "Nothing at all was received" in message


def test_timeout_raises_rather_than_exiting(clock):
    """It used to be `exit(1)` behind a return value wait_for_ready never gave.

    Callers can now report the failure themselves instead of the process dying
    with a bare status code.
    """
    with pytest.raises(HMTLReadyTimeout):
        _connect(clock, [(0.5, b"noise")])


# ---------------------------------------------------------------------------
# Framing: the reader vs. ESP32 boot-ROM noise
# ---------------------------------------------------------------------------

class _ReplayBuffer(InputBuffer):
    """An InputBuffer over a fixed byte string, for testing run()'s framing."""

    def __init__(self, data):
        InputBuffer.__init__(self, bufflen=100, verbose=False)
        self._data = data
        self._pos = 0

    def get_reader(self):
        return None

    def read(self, max_read):
        chunk = self._data[self._pos:self._pos + max_read]
        self._pos += len(chunk)
        return chunk

    def write(self, data):
        raise NotImplementedError

    def items(self):
        """Frame the whole input and return every item it produced."""
        produced = []
        while self._pos < len(self._data) or self._pushback:
            item = self._read_item()
            if item is not None:
                produced.append(item)
        return produced


class _ChunkedBuffer(InputBuffer):
    """An InputBuffer over a list of chunks, one `read()` per chunk.

    Unlike _ReplayBuffer this can yield b"" *in the middle* of the stream, which
    is what a 0.1 s serial read timeout looks like: the source is not finished,
    it has just gone quiet.  That gap is the interesting case -- it is where a
    partial frame gets stranded.
    """

    def __init__(self, chunks):
        InputBuffer.__init__(self, bufflen=100, verbose=False)
        self._chunks = list(chunks)
        self._pending = b""

    def get_reader(self):
        return None

    def read(self, max_read):
        if not self._pending:
            if not self._chunks:
                return b""
            self._pending = self._chunks.pop(0)
            if not self._pending:
                # An empty chunk *is* the read timeout being modelled.
                return b""
        chunk, self._pending = (self._pending[:max_read],
                                self._pending[max_read:])
        return chunk

    def write(self, data):
        raise NotImplementedError

    def drained(self):
        return not self._chunks and not self._pending and not self._pushback

    def items(self):
        produced = []
        while not self.drained():
            item = self._read_item()
            if item is not None:
                produced.append(item)
        return produced


def _hmtl_poll_frame(address=129):
    return HMTLprotocol.get_poll_msg(address)


def test_short_start_code_burst_then_read_timeout_does_not_kill_the_reader():
    """A truncated frame at a read boundary must not take the thread down.

    The exact sequence from the bench: boot-ROM noise whose last burst happens
    to end `...\\xfc\\x11`, then the >100 ms gap before the application banner,
    then the banner and `ready`.  Fewer than 8 bytes are in hand when the read
    times out, so the old code fell through to InputItem(is_hmtl=True) ->
    MsgHdr.from_data() -> `struct.error: unpack_from requires a buffer of at
    least 8 bytes`, straight out of InputBuffer.run().  The reader thread died
    and wait_for_ready() then sat there for 14 s reporting that nothing had been
    received -- on a bus that carries igniter-time noise, routinely.

    The reader must instead treat the stranded bytes as noise, resynchronise,
    and still deliver the `ready` behind them.
    """
    buffer_obj = _ChunkedBuffer([b"\xfc\x11", b"",
                                 b"HMTL Fire Control\r\n" + READY + b"\r\n"])

    payloads = [item.data for item in buffer_obj.items()]

    assert READY in payloads, (
        "reader did not resynchronise past the truncated frame")


def test_truncated_frame_at_a_read_boundary_is_not_parsed_as_a_header():
    """The stranded bytes are re-scanned, not emitted as a bogus HMTL item."""
    buffer_obj = _ChunkedBuffer([b"\xfc\x11\x22", b"", b"text\r\n"])

    items = buffer_obj.items()

    assert not any(item.is_hmtl for item in items), (
        "a sub-header-length fragment was emitted as an HMTL message")
    # The rewound noise has no line terminator after it, so it prefixes the
    # following text rather than becoming an item of its own -- ugly in the log,
    # but nothing is lost and nothing raises.
    assert b"text" in b"".join(item.data for item in items)


def test_lone_start_code_before_a_gap_is_dropped_without_looping():
    """A single 0xFC then silence: one byte consumed, no infinite rescan."""
    buffer_obj = _ChunkedBuffer([b"\xfc", b"", READY + b"\r\n"])

    payloads = [item.data for item in buffer_obj.items()]

    assert payloads == [READY]


@pytest.mark.parametrize("split", [1, 3, 7, 8, 9, 11])
def test_a_real_frame_split_across_a_read_still_arrives(split):
    """Resync must not eat a genuine message that merely arrives in pieces.

    This is the over-correction guard for the truncated-frame fix above: having
    taught the reader to throw away a stranded partial frame, it must not throw
    away a real one that is merely straddling a read timeout.  A 0.1 s gap in
    the middle of a 12-byte message is ordinary -- the USB adapter's latency
    timer, the far end filling its TX buffer, a busy host -- and the message on
    the other side of it is an actuator command.

    The frame is deliberately one WITH A BODY (`get_value_msg`, 12 bytes).  An
    earlier version of this test used `_hmtl_poll_frame()`, which is
    MSG_POLL_LEN == MSG_BASE_LEN == 8 bytes, so `frame[8:]` was `b""` and the
    "split" split off nothing: the frame completed at byte 8 before the gap was
    ever read, and the test passed on any behaviour whatsoever.  Splitting at
    every interesting offset -- inside the header, exactly at the header
    boundary, inside the body -- makes it impossible to be vacuous again.
    """
    frame = HMTLprotocol.get_value_msg(129, 0, 255)
    assert len(frame) > HMTLprotocol.MsgHdr.length(), (
        "fixture must have a body, or nothing is being split")

    buffer_obj = _ChunkedBuffer([frame[:split], b"", frame[split:]])

    items = buffer_obj.items()

    assert [item.data for item in items] == [frame], (
        "frame split at offset %d was not reassembled" % split)
    assert items[0].is_hmtl


def test_a_body_that_never_arrives_is_abandoned_rather_than_awaited_forever():
    """The other edge of the same fix: patience has to be bounded.

    Noise that happens to pass the header sanity check announces a body that
    will never come.  Waiting for it forever would wedge the reader just as
    surely as the struct.error crash did -- and no bogus header-only HMTL item
    may be emitted either.  After MAX_PARTIAL_FRAME_READS empty reads the bytes
    are abandoned (minus the false start code) and the stream resynchronises.
    """
    frame = HMTLprotocol.get_value_msg(129, 0, 255)
    gap = [b""] * (InputBuffer.MAX_PARTIAL_FRAME_READS + 2)
    buffer_obj = _ChunkedBuffer([frame[:HMTLprotocol.MsgHdr.length()]] +
                                gap + [READY + b"\r\n"])

    items = buffer_obj.items()

    assert not any(item.is_hmtl for item in items), (
        "a message with no body was emitted as if it were complete")
    assert READY in [item.data for item in items], (
        "reader did not resynchronise past the abandoned frame")


def test_boot_noise_then_banner_then_ready_still_yields_ready():
    """The ESP32 ROM prints at ~74880 baud; at 115200 that is binary garbage.

    Some of those bytes are 0xFC.  The reader used to take any 0xFC as a message
    start, read a bogus length out of the noise, and swallow up to 255 following
    bytes -- eating the banner and the `ready` line behind it.
    """
    noise = bytes([0xFC, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                   0xFC, 0xAA, 0xBB, 0xCC])
    stream = noise + b"HMTL Fire Control\r\n" + READY + b"\r\n"

    payloads = [item.data for item in _ReplayBuffer(stream).items()]

    assert READY in payloads, (
        "the ready line was swallowed by a false message frame")


def test_valid_message_after_noise_is_still_framed():
    """Resyncing must not cost us the real message that follows the noise."""
    noise = bytes([0xFC, 0x00, 0x99, 0x02, 0x01, 0x00, 0x00, 0x00])
    frame = _hmtl_poll_frame()
    stream = noise + frame

    items = _ReplayBuffer(stream + b"\r\n").items()
    hmtl_items = [item for item in items if item.is_hmtl]

    assert any(item.data == frame for item in hmtl_items)


def test_text_immediately_followed_by_a_frame_is_not_merged():
    """An unterminated line before a start code must not corrupt the header."""
    frame = _hmtl_poll_frame()
    stream = b"partial" + frame

    items = _ReplayBuffer(stream).items()

    assert [item.data for item in items] == [b"partial", frame]


def test_invalid_header_version_is_rejected():
    buffer_obj = _ReplayBuffer(b"")
    bad = HMTLprotocol.MsgHdr.from_data(
        bytes([0xFC, 0x00, 0x07, 0x08, 0x02, 0x00, 0x00, 0x00]))

    assert not buffer_obj._valid_hmtl_header(bad)


def test_undecodable_bytes_are_printable_not_fatal():
    """A raw item must render for the log without raising."""
    item = InputItem.from_data(b"\xff\xfe\x80binary")

    assert "raw" in str(item)


# ---------------------------------------------------------------------------
# Baud: no default
# ---------------------------------------------------------------------------

def test_serial_buffer_has_no_default_baud():
    """A default baud is wrong for every real device, so there isn't one.

    AVR HMTL modules are 57600 and the ESP32 console is 115200; the old default
    of 9600 silently produced unreadable bytes and then a ready timeout.
    """
    signature = inspect.signature(SerialBuffer.__init__)

    assert signature.parameters["baud"].default is inspect.Parameter.empty


# ---------------------------------------------------------------------------
# Not resetting the device on connect
# ---------------------------------------------------------------------------

class _FakeSerial:
    """Records the order of attribute writes relative to open()."""

    def __init__(self):
        self.events = []
        self.is_open = False

    def __setattr__(self, name, value):
        if name in ("port", "baudrate", "timeout", "dtr", "rts", "dsrdtr"):
            self.events.append((name, value))
        object.__setattr__(self, name, value)

    def open(self):
        self.events.append(("open", None))
        self.is_open = True

    def fileno(self):
        raise OSError("not a real fd")


def test_open_skips_pyserials_dtr_write_and_lowers_dtr_afterwards(monkeypatch):
    """The shape of the fix, at the pyserial API level.

    `dsrdtr` and `rts` must be set BEFORE open() (pyserial applies both inside
    it), and `dtr` must be written AFTER, so DTR comes down only once RTS is
    already down.  See _open_port()'s docstring for why that order is the whole
    point, and test_open_never_leaves_dtr_low_while_rts_is_high for the ioctl
    sequence that actually reaches the port.
    """
    fake = _FakeSerial()
    monkeypatch.setattr("hmtl.SerialBuffer.serial.Serial", lambda: fake)

    SerialBuffer._open_port("/dev/fake", 115200, 0.1, reset_on_open=False)

    names = [name for name, _ in fake.events]
    assert names.index("dsrdtr") < names.index("open"), (
        "dsrdtr must be set before open() or pyserial writes DTR itself")
    assert names.index("rts") < names.index("open")
    assert names.index("dtr") > names.index("open"), (
        "writing dtr before open() puts the DTR change back inside open(), "
        "ahead of the RTS change -- the reset state this exists to avoid")
    assert dict(fake.events)["dsrdtr"] is True
    assert dict(fake.events)["rts"] is False
    assert dict(fake.events)["dtr"] is False


def test_reset_on_open_is_opt_in(monkeypatch):
    """The old reset-on-connect behaviour is still reachable, explicitly."""
    calls = []
    monkeypatch.setattr("hmtl.SerialBuffer.serial.Serial",
                        lambda *args, **kwargs: calls.append((args, kwargs)))

    SerialBuffer._open_port("/dev/fake", 57600, 0.1, reset_on_open=True)

    assert calls == [(("/dev/fake", 57600), {"timeout": 0.1})]


# ---------------------------------------------------------------------------
# Platform behaviour, verified rather than assumed (POSIX: macOS and Linux)
# ---------------------------------------------------------------------------

@pytest.mark.skipif(not hasattr(termios, "HUPCL"), reason="no termios.HUPCL")
def test_pyserial_leaves_hupcl_set_so_we_have_to_clear_it():
    """Pins the platform assumption behind _disable_hupcl().

    pyserial's _reconfigure_port() never touches HUPCL, so the tty layer drops
    DTR when the last fd closes.  On a board with an auto-reset circuit that
    means the device reboots when *we exit* -- the failure is at the end of the
    session, not the start, which is why it is easy to miss.
    """
    import serial

    master, slave = pty.openpty()
    port = os.ttyname(slave)
    try:
        conn = serial.Serial()
        conn.port = port
        conn.baudrate = 115200
        conn.timeout = 0.1
        conn.dtr = False
        conn.rts = False
        conn.open()
        try:
            attrs = termios.tcgetattr(conn.fileno())
            assert attrs[2] & termios.HUPCL, (
                "pyserial now clears HUPCL itself; _disable_hupcl() can go")

            assert _disable_hupcl(conn) is True
            attrs = termios.tcgetattr(conn.fileno())
            assert not (attrs[2] & termios.HUPCL)
        finally:
            conn.close()
    finally:
        os.close(master)
        try:
            os.close(slave)
        except OSError:
            pass


@pytest.mark.skipif(not hasattr(termios, "HUPCL"), reason="no termios.HUPCL")
def test_serial_buffer_opens_a_real_port_without_reset_and_clears_hupcl():
    """End-to-end on a pty: the real open path, on this platform."""
    master, slave = pty.openpty()
    port = os.ttyname(slave)
    try:
        buff = SerialBuffer(port, 115200, verbose=False)
        try:
            assert buff.device == port
            assert buff.baud == 115200
            # NOTE: deliberately NOT asserting buff.connection.dtr/.rts here.
            # Those are getters for pyserial's cached _dtr_state/_rts_state, not
            # a TIOCMGET of the line, so they would pass whatever the wire did.
            # A pty has no modem lines at all (TIOCMGET returns ENOTTY on
            # macOS), so the line state genuinely cannot be observed from here.
            # What IS observable, and is checked, is the sequence of ioctls the
            # open path issues -- see
            # test_open_never_leaves_dtr_low_while_rts_is_high.  The wire itself
            # is a `## Testing Required` bench item with a scope on DTR/RTS/EN.
            attrs = termios.tcgetattr(buff.connection.fileno())
            assert not (attrs[2] & termios.HUPCL), (
                "closing the port would drop DTR and reset the device")
        finally:
            buff.connection.close()
    finally:
        os.close(master)
        try:
            os.close(slave)
        except OSError:
            pass


@pytest.mark.skipif(not hasattr(termios, "HUPCL"), reason="POSIX only")
def test_open_never_leaves_dtr_low_while_rts_is_high(monkeypatch):
    """The ESP32 auto-reset state must never be visited during open().

    This is the closest a host-side test can get to the wire.  A pty has no
    modem lines, so instead of reading them back we record the modem-line ioctls
    the open path actually issues (TIOCMBIS/TIOCMBIC with the DTR/RTS bits) and
    replay them against the ESP32 auto-reset circuit's truth table:

        DTR RTS  ->  EN  IO0
         1   1        1   1
         0   0        1   1
         1   0        1   0
         0   1        0   1   <-- reset asserted

    The OS raises both lines on os.open(), so the walk starts at (1, 1).  The
    naive `dtr = False; rts = False` before open() makes pyserial write DTR
    first (serialposix.open() calls _update_dtr_state() then
    _update_rts_state()), stepping through (0, 1) -- EN low.  That is the bug
    this ordering exists to avoid, and reverting the fix turns this test red.
    """
    import struct
    import fcntl as real_fcntl
    import serial.serialposix as serialposix

    line_ops = []
    bit_names = {serialposix.TIOCM_DTR: "DTR", serialposix.TIOCM_RTS: "RTS"}

    class _RecordingFcntl:
        """Delegates to fcntl, recording (and faking) the modem-line ioctls."""

        def ioctl(self, fd, request, arg=0, mutate_flag=True):
            if request in (serialposix.TIOCMBIS, serialposix.TIOCMBIC):
                bits = struct.unpack("I", arg)[0]
                line_ops.append((bit_names[bits],
                                 request == serialposix.TIOCMBIS))
                # A pty would raise ENOTTY; report success so the real code
                # path runs to completion and we see every write it makes.
                return arg
            return real_fcntl.ioctl(fd, request, arg, mutate_flag)

        def __getattr__(self, name):
            return getattr(real_fcntl, name)

    monkeypatch.setattr(serialposix, "fcntl", _RecordingFcntl())

    master, slave = pty.openpty()
    port = os.ttyname(slave)
    try:
        conn = SerialBuffer._open_port(port, 115200, 0.1, reset_on_open=False)
        conn.close()
    finally:
        os.close(master)
        try:
            os.close(slave)
        except OSError:
            pass

    assert line_ops, "no modem-line ioctls were issued at all"

    dtr, rts = True, True  # as the kernel left them at os.open()
    for name, asserted in line_ops:
        if name == "DTR":
            dtr = asserted
        else:
            rts = asserted
        assert not (rts and not dtr), (
            "open() passed through DTR-low/RTS-high (EN low on an ESP32) "
            "after %r; full sequence was %r" % ((name, asserted), line_ops))

    assert (dtr, rts) == (False, False), (
        "both lines must end deasserted, ended %r" % ((dtr, rts),))


def test_disable_hupcl_is_harmless_without_a_real_fd():
    class _NoFd:
        def fileno(self):
            raise OSError("not a tty")

    assert _disable_hupcl(_NoFd()) is False


# ---------------------------------------------------------------------------
# The command line tools
# ---------------------------------------------------------------------------

_BIN = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "bin")

# A path that is not a device, so argument checking is reached but nothing is
# ever opened.  Every tool must reject the missing baud before it gets there.
_NOT_A_DEVICE = "/nonexistent/hmtl-test-tty"


@pytest.mark.parametrize("tool,expected", [
    ("HMTLCommandServer", 115200),
    ("HMTLConfig", 9600),
    ("TailArduino", 9600),
])
def test_serial_tools_keep_their_historical_baud_defaults(tool, expected):
    """Each tool defaults to the baud of the sketches it actually talks to.

    These differ ON PURPOSE and must not be unified (Adam, PR #14): HMTLConfig
    and TailArduino drive the setup/test sketches, which all run
    Serial.begin(9600) -- HMTLPythonConfig.ino:63, HMTL_Bringup.ino:65,
    HMTL_Upgrade_Version.ino:35.  HMTLCommandServer talks to a running module
    or an ESP32 console, hence 115200.

    This branch briefly made --baud required on all three, which broke every
    existing invocation.  This test exists so that cannot recur silently.
    """
    import subprocess

    result = subprocess.run(
        [sys.executable, os.path.join(_BIN, tool), "--help"],
        capture_output=True, text=True,
        env=dict(os.environ, PYTHONPATH=os.path.dirname(_BIN)))

    assert result.returncode == 0
    assert "[%d]" % expected in result.stdout, (
        "%s should advertise %d as its default baud" % (tool, expected))


def test_command_server_defaults_to_115200():
    """HMTLCommandServer runs without --baud, at the ESP32 console rate.

    Guards the restoration of the pre-branch default (PR #14).  Asserts the
    tool does not exit demanding a baud -- the failure it must NOT produce.
    """
    import subprocess

    result = subprocess.run(
        [sys.executable, os.path.join(_BIN, "HMTLCommandServer"), "-d", _NOT_A_DEVICE],
        capture_output=True, text=True,
        env=dict(os.environ, PYTHONPATH=os.path.dirname(_BIN)))

    combined = result.stdout + result.stderr
    assert "Must specify --baud" not in combined, \
        "HMTLCommandServer must not demand --baud; it defaults to 115200"
