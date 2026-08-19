################################################################################
# Author: Adam Phelps
# License: MIT
# Copyright: 2015
#
# This class reads from a serial device into a circular message buffer.  The
# data read can either be line terminated ('\n') or as HMTL messages
################################################################################

import errno

import serial

from hmtl.TimedLogger import TimedLogger
from hmtl.InputBuffer import InputBuffer

try:
    import termios
except ImportError:  # pragma: no cover - non-POSIX
    termios = None


def _disable_hupcl(connection):
    """Clear HUPCL so the tty layer does not drop DTR on the last close.

    DEFENSIVE, not load-bearing on the normal path.  HUPCL makes the tty layer
    *lower* DTR/RTS when the last fd closes -- but `_open_port()` has already
    lowered both for the whole session, so on that path the drop at exit is
    electrically a no-op: no falling edge for an AVR's DTR cap, no EN-low state
    for an ESP32.  It matters only if something re-asserts the lines mid-session
    (a `conn.dtr = True`, another process opening the same port), which is
    exactly the case worth being cheap insurance against.  The one path where it
    would really be needed -- `reset_on_open=True` -- deliberately does not call
    it, because there the reset is the point.

    Best effort: silently does nothing where the platform or the file
    descriptor does not support it (no termios, not a tty, a pty in a test).
    Returns True when HUPCL was actually cleared, which is what the unit tests
    assert against.
    """
    if termios is None:
        return False
    try:
        fd = connection.fileno()
        attrs = termios.tcgetattr(fd)
        attrs[2] &= ~termios.HUPCL  # index 2 == cflag
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
    except Exception:
        return False
    return True


class SerialBuffer(InputBuffer):
    """
    This class reads from a serial port into a circular buffer.  It reads
    until it gets to the end of a line or the end of an HMTL message.
    """

    # Default logging color
    LOGGING_COLOR = TimedLogger.CYAN

    def __init__(self, device, baud, timeout=0.1, bufflen=1000,
                 verbose=True, reset_on_open=False):
        """Open `device` at `baud`.

        `baud` is deliberately positional and has NO default: there is no baud
        that is right for more than one kind of HMTL device (AVR modules run at
        57600, the ESP32 console at 115200, older boards at 9600).  A default
        turns a wrong-baud connection into a confusing symptom -- garbage bytes
        followed by a ready timeout -- so every caller must say what it means.

        `reset_on_open` defaults to False so that attaching to a device does
        not reboot it.  That holds for an ESP32; an AVR board's DTR-capacitor
        reset fires inside `os.open()` and cannot be suppressed from software.
        See `_open_port()`.
        """
        InputBuffer.__init__(self, bufflen, verbose)

        # Open the serial connection
        self.device = device
        self.baud = baud
        self.connection = self._open_port(device, baud, timeout, reset_on_open)
        self.logger.log("SerialBuffer: connected to %s at %s baud%s" %
                        (device, baud,
                         "" if reset_on_open else " (no reset requested on open)"),
                        color=TimedLogger.CYAN)

    @staticmethod
    def _open_port(device, baud, timeout, reset_on_open):
        """Open the port, by default without resetting the attached device.

        Most USB-serial adapters wire DTR (AVR boards) or DTR+RTS (the ESP32
        auto-reset circuit) to the MCU's reset line, so a plain
        `serial.Serial(device, ...)` reboots whatever is on the other end.  That
        is the wrong default here: the command server is often attached to a
        controller that is already running a show.

        The kernel raises DTR *and* RTS on `os.open()`, before any Python of
        ours runs, so what we control is only the order in which they come back
        down.  That order is the whole game on an ESP32: its auto-reset circuit
        holds EN low exactly when DTR is low while RTS is still high.

        pyserial 3.5's `open()` writes DTR before RTS (`_update_dtr_state()`
        then `_update_rts_state()` in serialposix), so the obvious
        `dtr = False; rts = False` before `open()` walks straight through
        DTR-low/RTS-high -- i.e. through the reset condition it was written to
        avoid.  Instead:

        1. `dsrdtr = True` before `open()` makes pyserial skip its in-open DTR
           write, leaving DTR asserted as the kernel left it.  (On POSIX
           `dsrdtr` has no other effect: `_reconfigure_port()` never reads it.)
           `open()` then lowers RTS only -- DTR high, RTS low, which the circuit
           reads as IO0-low, harmless while EN stays high.
        2. `dtr = False` immediately after `open()` brings DTR down with RTS
           already low, so both end deasserted without EN ever going low.
        3. HUPCL is cleared afterwards -- see `_disable_hupcl()`.  On this path
           it is insurance, not the thing preventing a reset: the lines are
           already low, so the drop at close has no edge to give.

        What this does and does not buy, honestly:

        - **ESP32**: the software half is now complete -- no state we can reach
          from Python is an EN-low state.  Whether the adapter itself glitches
          the lines during `os.open()` is per-driver (FTDI vs CP2102 vs CH340)
          and is only observable on a scope; hence the bench item in the plan's
          `## Testing Required`.
        - **AVR boards (e.g. module 72's FTDI)**: NOT avoidable.  Their reset is
          edge-triggered through a DTR capacitor and the rising edge happens
          inside `os.open()`, before this function regains control.  No pyserial
          ordering, and no termios setting, prevents that.  Connecting to an AVR
          module still reboots it.
        """
        if reset_on_open:
            return serial.Serial(device, baud, timeout=timeout)

        conn = serial.Serial()
        conn.port = device
        conn.baudrate = baud
        conn.timeout = timeout

        if termios is not None:
            # POSIX ordering, per (1) and (2) above.  Not used off POSIX, where
            # dsrdtr means real DTR/DSR flow control and would stall the link.
            conn.dsrdtr = True
            conn.rts = False
            conn.open()
            try:
                conn.dtr = False
            except OSError as e:
                # Some ports have no modem lines at all (a pty, a USB CDC-ACM
                # gadget).  pyserial swallows exactly these two inside open()
                # for the same reason; there is nothing to lower, so nothing to
                # do.  Anything else is a real failure and should propagate.
                if e.errno not in (errno.EINVAL, errno.ENOTTY):
                    raise
        else:  # pragma: no cover - non-POSIX
            conn.dtr = False
            conn.rts = False
            conn.open()

        _disable_hupcl(conn)
        return conn

    def get_reader(self):
        return self.connection

    def read(self, max_read):
        try:
            return self.connection.read(max_read)
        except serial.SerialException:
            return None

    def write(self, data):
        return self.connection.write(data)

    def stop(self):
        self.connection.close()
        super(SerialBuffer, self).stop()
