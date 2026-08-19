################################################################################
# Author: Adam Phelps
# License: MIT
# Copyright: 2014
#
# Class for handling the serial communications with an HMTL device
#
################################################################################

from binascii import hexlify
import time

import hmtl.HMTLprotocol as HMTLprotocol
from hmtl.TimedLogger import TimedLogger


class HMTLConfigException(Exception):
    pass


class HMTLReadyTimeout(Exception):
    """No `ready` was seen from the device within MAX_READY_WAIT."""
    pass


class HMTLSerial():

    # Default logging color
    LOGGING_COLOR = TimedLogger.WHITE

    #
    # Firmware constants this class must stay consistent with.  Both live in
    # Libraries/HMTLMessaging/MessageHandler.h (READY_THRESHOLD /
    # READY_RESEND_PERIOD, in milliseconds); the values here are seconds.
    #
    # MessageHandler::serial_ready() resends "ready" only once the module has
    # seen no serial traffic for READY_THRESHOLD, and then no more often than
    # READY_RESEND_PERIOD.  That resend is the *only* announcement a client gets
    # when the device is not reset by opening the port -- which is now the
    # normal case, since SerialBuffer deliberately does not assert DTR/RTS.
    #
    FIRMWARE_READY_THRESHOLD = 10.0
    FIRMWARE_READY_RESEND_PERIOD = 1.0

    # How long to wait for the ready signal after connection.
    #
    # This MUST exceed FIRMWARE_READY_THRESHOLD + FIRMWARE_READY_RESEND_PERIOD,
    # or the client gives up before the first possible resend and the resend
    # path can never rescue a missed `ready`.  It used to be a flat 10 s -- the
    # same value as READY_THRESHOLD -- so the firmware's first retry landed at
    # or after the moment the client bailed out.  The extra margin covers serial
    # latency and a slow boot banner.
    MAX_READY_WAIT = FIRMWARE_READY_THRESHOLD + FIRMWARE_READY_RESEND_PERIOD + 3.0

    # How many recently seen lines to quote back in a ready-timeout error.
    TIMEOUT_CONTEXT_LINES = 5

    def __init__(self, buff, verbose=False):
        '''Open a serial connection and wait for the ready signal'''
        self.verbose = verbose
        self.last_received = 0
        self.serial = buff

        # Create the logger
        self.logger = TimedLogger(self.serial.start_time, textcolor=self.LOGGING_COLOR)

        self.serial.start()
        self.wait_for_ready()

    def get_message(self, timeout=None):
        """Returns the next line of text or a complete HMTL message"""

        item = self.serial.get(wait=timeout)

        if not item:
            return None

        self.last_received = time.time()

        return item

    def device_description(self):
        """Best-effort '<device> at <baud> baud' for error messages."""
        device = getattr(self.serial, "device", None)
        baud = getattr(self.serial, "baud", None)
        if device is None:
            return type(self.serial).__name__
        if baud is None:
            return str(device)
        return "%s at %s baud" % (device, baud)

    # Wait for data from device indicating its ready for commands
    def wait_for_ready(self):
        """Wait for the device to send its ready signal.

        Returns True once `ready` is seen; raises HMTLReadyTimeout otherwise.

        There is deliberately no "discard anything that arrives in the first
        half second" window here.  That window existed to drop output from
        before the module reset, but it also threw away the `ready` of a board
        that boots fast -- and since the only exact match is the literal
        HMTL_CONFIG_READY line, a stale one is not misleading anyway: it still
        means a module on the far end is up and listening.
        """
        self.logger.log("***** Waiting for ready from %s *****" %
                        self.device_description())
        start_wait = time.time()
        seen = []
        while True:
            item = self.get_message(1.0)

            if item:
                if item.data == HMTLprotocol.HMTL_CONFIG_READY:
                    self.logger.log("***** Received ready *****")
                    return True
                seen.append(str(item))
                del seen[:-self.TIMEOUT_CONTEXT_LINES]

            if (time.time() - start_wait) > self.MAX_READY_WAIT:
                raise HMTLReadyTimeout(self._timeout_message(seen))

    def _timeout_message(self, seen):
        """Explain a ready timeout in terms someone can act on."""
        if seen:
            context = ("Last %d item(s) received:\n  %s" %
                       (len(seen), "\n  ".join(seen)))
        else:
            context = ("Nothing at all was received -- check the cable, that "
                       "no other process holds the port, and the baud rate.")
        return (
            "Timed out after %.1fs waiting for the %r signal from %s.\n"
            "%s\n"
            "A wrong baud rate is the usual cause (AVR HMTL modules: 57600, "
            "ESP32 console: 115200); a device that is not running HMTL "
            "firmware is the other." % (
                self.MAX_READY_WAIT,
                HMTLprotocol.HMTL_CONFIG_READY.decode(),
                self.device_description(),
                context))

    # Send terminated data and wait for (N)ACK
    def send_and_confirm(self, data, terminated, timeout=10):
        """Send a command and wait for the ACK"""

        self.serial.write(data)
        if (terminated):
            self.serial.write(HMTLprotocol.HMTL_TERMINATOR)

        start_wait = time.time()
        while True:
            item = self.get_message()
            if item is not None:
                if item.data == HMTLprotocol.HMTL_CONFIG_ACK:
                    return True
                if item.data == HMTLprotocol.HMTL_CONFIG_FAIL:
                    raise HMTLConfigException("Configuration command failed")
            if (time.time() - start_wait) > timeout:
                raise Exception("Timed out waiting for ACK signal")


# XXX: Here we need a method of getting data back from poll or the like

    # Send a text command
    def send_command(self, command):
        self.logger.log("send_command: %s" % (command))
        self.send_and_confirm(command, True)

    # Send a binary config update
    def send_config(self, type, config):
        self.logger.log("send_config:  %-10s %s" % (type, hexlify(config)))
        self.send_and_confirm(config, True)