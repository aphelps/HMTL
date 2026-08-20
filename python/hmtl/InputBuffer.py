################################################################################
# Author: Adam Phelps
# License: MIT
# Copyright: 2015
#
# This abstract class reads from a generic reader into a circular message
# buffer.  The data read can either be line terminated ('\n') or as HMTL
# messages.
#
################################################################################

from binascii import hexlify
import threading
import time

from hmtl.CircularBuffer import CircularBuffer
from hmtl.TimedLogger import TimedLogger
import hmtl.HMTLprotocol as HMTLprotocol

from abc import ABCMeta, abstractmethod


class InputBuffer(threading.Thread, metaclass=ABCMeta):

    # Default logging color
    LOGGING_COLOR = TimedLogger.CYAN

    # `msg_hdr_t.length` is a single byte, so nothing longer can be described
    # by a valid header.  A "length" above this came from noise, not a message.
    MAX_HMTL_MSG_LEN = 255

    # How many consecutive empty reads to ride out while a *partial* HMTL frame
    # is in hand before concluding that it was never a frame at all.  On serial
    # each one is a full read timeout (0.1 s by default), so this is ~0.5 s of
    # patience.  It has to be greater than zero: a 12-byte message straddling a
    # read boundary is ordinary -- the adapter's latency timer, the far end
    # filling its TX buffer, a busy host -- and abandoning the frame there loses
    # an actuator command silently.  It also has to be bounded: noise that
    # happens to look like a valid header must not wedge the reader waiting for
    # a body that is never coming.
    MAX_PARTIAL_FRAME_READS = 5

    def __init__(self, bufflen=1000, verbose=True):
        threading.Thread.__init__(self)

        self.verbose = verbose

        self.last_received = 0
        self.total_received = 0

        # Bytes consumed by a framing attempt that turned out to be noise, to
        # be re-scanned before reading anything new.  See _rewind().
        self._pushback = []

        # Create the buffer for storing serial data
        self.buff = CircularBuffer(bufflen)

        self.start_time = time.time()
        self.logger = TimedLogger(self.start_time, textcolor=self.LOGGING_COLOR)

        # Set as a daemon so that this thread will exit correctly
        # when the parent receives a kill signal
        self.daemon = True

    @abstractmethod
    def get_reader(self):
        pass

    @abstractmethod
    def read(self, max_read):
        pass

    @abstractmethod
    def write(self, data):
        pass

    def get_buffer(self):
        return self.buff

    def get(self, wait=None):
        return self.buff.get(wait=wait)

    def stop(self):
        # Signal the thread to stop by setting daemon=True and letting it die
        # with the parent, or interrupt its blocking read via the buffer.
        pass

    def _read_unit(self):
        """Read one unit, taking anything previously rewound first."""
        if self._pushback:
            return self._pushback.pop(0)

        char = self.read(1)
        if char:
            self.total_received += 1
        return char

    def _rewind(self, data):
        """Push `data` back to be re-scanned as if it had never been consumed.

        Sliced one unit at a time rather than iterated, so this works for both
        the bytes readers (serial, socket) and the str one (stdin) without
        caring which it is.
        """
        self._pushback[:0] = [data[i:i + 1] for i in range(len(data))]

    def _valid_hmtl_header(self, hdr):
        """Does this look like a real message header, or like line noise?

        The start code alone is one byte out of 256, and a serial line carries
        plenty of bytes that are not messages -- most sharply, the ESP32 boot
        ROM prints at ~74880 baud, so on a port opened at 115200 its output
        arrives as arbitrary binary before the application banner.  Accepting a
        bogus header there is not harmless: `hdr.length` then tells the reader
        to swallow up to 255 following bytes, which is easily the whole boot
        banner *and* the `ready` line the caller is waiting for.
        """
        return (hdr.version == HMTLprotocol.MsgHdr.PROTOCOL_VERSION and
                HMTLprotocol.MsgHdr.length() <= hdr.length <= self.MAX_HMTL_MSG_LEN)

    @staticmethod
    def _frame_complete(data, hdr):
        """Is `data` a whole HMTL message, or still waiting on more bytes?

        `hdr` is None until 8 bytes have been collected, so "no header yet"
        is by definition incomplete.
        """
        return hdr is not None and len(data) >= hdr.length

    def run(self):
        while True:
            self._read_item()

    def _read_item(self):
        """Frame one item off the stream and queue it.

        Split out of run() so the framing can be exercised directly against a
        fixed byte string.  Returns the item, or None if the source ran dry
        before anything complete arrived.
        """
        data = b""
        is_hmtl = False
        hdr = None
        empty_reads = 0
        while True:
            char = self._read_unit()

            if (char is None) or (len(char) == 0):
                # The source ran dry -- a read timeout (0.1 s on serial) or the
                # end of the stream.
                if is_hmtl and not self._frame_complete(data, hdr):
                    # A *partial* HMTL frame is in hand.  Two things must both
                    # be true here, and getting either wrong is a silent bug:
                    #
                    # 1. A real message that merely straddles a read boundary
                    #    must survive.  A gap inside a 12-byte frame is
                    #    ordinary, and the far side of it is an actuator
                    #    command -- emitting the header on its own and letting
                    #    the body fall out as text loses it without a word.  So
                    #    keep reading across a bounded number of empty reads.
                    #
                    # 2. Bytes that were never a frame must not be parsed as
                    #    one.  Fewer than 8 bytes with `is_hmtl` set would send
                    #    InputItem() into MsgHdr.from_data() and raise
                    #    struct.error straight out of the reader thread, which
                    #    then exits and takes every subsequent line with it --
                    #    including the `ready` the caller is waiting for.  ESP32
                    #    boot-ROM noise ends a burst on a 0xFC often enough, and
                    #    the gap before the app banner is far longer than a read
                    #    timeout.
                    #
                    # So: wait, then give up.  Giving up re-scans the bytes
                    # minus the false start code; dropping exactly one byte per
                    # attempt guarantees forward progress rather than a rescan
                    # loop.
                    empty_reads += 1
                    if empty_reads <= self.MAX_PARTIAL_FRAME_READS:
                        continue

                    if self.verbose:
                        self.logger.log(
                            "Abandoning incomplete message, resyncing: %s"
                            % hexlify(data).decode())
                    self._rewind(data[1:])
                    return None
                break

            empty_reads = 0

            if not is_hmtl and ord(char) == HMTLprotocol.MsgHdr.STARTCODE:
                if data:
                    # A start code straight after unterminated text.  Emit
                    # the text as its own item and re-scan the start code,
                    # rather than parsing a header out of the text bytes.
                    self._rewind(char)
                    break
                # This is the start of an HMTL data message
                is_hmtl = True

            if is_hmtl:
                data += char

                if len(data) == HMTLprotocol.MsgHdr.length():
                    # Received enough data for a full message header
                    hdr = HMTLprotocol.MsgHdr.from_data(data)

                    if not self._valid_hmtl_header(hdr):
                        # Not a message: the start code was noise.  Drop
                        # just that byte and re-scan the rest, so whatever
                        # follows inside those bytes is still seen.
                        if self.verbose:
                            self.logger.log(
                                "Discarding false start code, resyncing: %s"
                                % hexlify(data).decode())
                        self._rewind(data[1:])
                        data = b""
                        is_hmtl = False
                        hdr = None
                        continue

                if hdr:
                    if len(data) >= hdr.length:
                        # Reached end of message
                        break
            else:
                # Arduino print output lines are terminated with \r\n
                if char == b'\r':
                    continue
                if char == b'\n':
                    break
                data += char

        if data and len(data):
            self.last_received = time.time()
            item = InputItem(data, self.last_received, is_hmtl)
            self.buff.put(item)

            if self.verbose:
                item.print(self.logger)

            return item

        return None


class InputItem:
    """
    Class containing data from a single serial item
    """

    def __init__(self, data, timestamp, is_hmtl=False):
        self.data = data
        self.timestamp = timestamp
        self.is_hmtl = is_hmtl
        self.hdr = HMTLprotocol.MsgHdr.from_data(data) if is_hmtl else None

    @staticmethod
    def from_data(data, timestamp=None):
        if timestamp is None:
            timestamp = time.time()

        if len(data) == 0:
            return None

        if data[0] == HMTLprotocol.MsgHdr.STARTCODE:
            is_hmtl = True
            # TODO: Perform validation here
        else:
            is_hmtl = False

        return InputItem(data, timestamp, is_hmtl)

    def __str__(self):
        if self.is_hmtl:
            return "(%s) '%s'" % (self.hdr.msg_type(), hexlify(self.data).decode())
        else:
            try:
                # TODO - This should really check the type of self.data, sometimes its str and sometimes binary
                return self.data.decode()
            except AttributeError:
                return self.data
            except UnicodeDecodeError:
                # Binary that is not a message -- boot-ROM noise, a truncated
                # frame.  Printable, never fatal.
                return "(raw) '%s'" % (hexlify(self.data).decode())

    def print(self, logger, color=None):
        logger.log(str(self), self.timestamp, color)