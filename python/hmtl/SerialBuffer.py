################################################################################
# Author: Adam Phelps
# License: MIT
# Copyright: 2015
#
# This class reads from a serial device into a circular message buffer.  The
# data read can either be line terminated ('\n') or as HMTL messages
################################################################################

import serial

from hmtl.TimedLogger import TimedLogger
from hmtl.InputBuffer import InputBuffer

try:
    import termios
except ImportError:  # pragma: no cover - non-POSIX
    termios = None


def _disable_hupcl(connection):
    """Clear HUPCL so closing the port does not drop DTR (and reset the device).

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

        `reset_on_open` defaults to False so that attaching to a device does not
        reboot it; see `_open_port()`.
        """
        InputBuffer.__init__(self, bufflen, verbose)

        # Open the serial connection
        self.device = device
        self.baud = baud
        self.connection = self._open_port(device, baud, timeout, reset_on_open)
        self.logger.log("SerialBuffer: connected to %s at %s baud%s" %
                        (device, baud, "" if reset_on_open else " (no reset on open)"),
                        color=TimedLogger.CYAN)

    @staticmethod
    def _open_port(device, baud, timeout, reset_on_open):
        """Open the port, by default without resetting the attached device.

        Most USB-serial adapters wire DTR (AVR boards) or DTR+RTS (the ESP32
        auto-reset circuit) to the MCU's reset line, so a plain
        `serial.Serial(device, ...)` reboots whatever is on the other end.  That
        is the wrong default here: the command server is often attached to a
        controller that is already running a show.

        Two things have to be suppressed, and they are separate:

        1. **Open.** pyserial records the DTR/RTS state on the object and applies
           it inside `open()`, so the state must be set *before* opening -- with a
           port passed to the constructor, `serial.Serial(...)` opens immediately
           and the lines are asserted first and lowered afterwards.
        2. **Close.** pyserial never touches HUPCL, so the tty layer drops DTR
           when the last fd closes -- i.e. the reset happens when our process
           *exits*, not when it starts.  Clearing HUPCL is what stops that.

        Neither step is a guarantee at the electrical level: on POSIX the driver
        may still assert the modem lines for the instant between `os.open()` and
        pyserial applying our state.  This is the best the API allows, and the
        residual behaviour is per-platform/per-driver -- hence the hardware
        check in the plan's `## Testing Required`.
        """
        if reset_on_open:
            return serial.Serial(device, baud, timeout=timeout)

        conn = serial.Serial()
        conn.port = device
        conn.baudrate = baud
        conn.timeout = timeout
        # Set before open(): see (1) above.
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
