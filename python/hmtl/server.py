################################################################################
# Author: Adam Phelps
# License: MIT
# Copyright: 2014
#
# Class that maintains a connection to an HMTL device and listens for commands
# over an IP port.
#
################################################################################

from multiprocessing.connection import Listener
import threading
import traceback

from hmtl.HMTLSerial import *
from hmtl.InputBuffer import InputItem
from hmtl.TimedLogger import TimedLogger

SERVER_ACK = "ack"
SERVER_EXIT = "exit"
SERVER_DATA_REQ = "data"


class HMTLServer():
    address = ('localhost', 6000)

    # Default logging color
    LOGGING_COLOR = TimedLogger.RED

    # How long to wait for a response from the device.  This must allow for a
    # message being relayed over RS485 to a remote module and back, which is
    # considerably slower than a reply from the directly attached module.
    DATA_TIMEOUT = 1.0

    # Timeout used when sweeping addresses that are mostly expected to be
    # empty.  Kept short deliberately: the scanner pays this cost once per
    # unused address per pass, and holds serial_cv while it waits.
    SCAN_TIMEOUT = 0.25

    def __init__(self, serial_device, address, device_scan=False, logger=True, verbose=True):
        self.ser = serial_device
        self.address = address

        self.logger = TimedLogger(self.ser.serial.start_time,
                                  textcolor=self.LOGGING_COLOR)
        if not logger:
            self.logger.disable()

        self.terminate = False

         # TODO: Is this needed at all? If so should the SerialBuffer handle synchronization?
        self.serial_cv = threading.Condition()

        self.conn = None
        self.listener = None

        # Whether the in-flight request has already been replied to
        self.replied = False

        self.verbose = verbose

        if device_scan:
            self.scanner = DeviceScanner(self, verbose)
            self.scanner.start()
        else:
            self.scanner = None


    def get_connection(self):
        try:
            self.logger.log("Waiting for connection")
            self.listener = Listener(self.address, authkey=b'secret password')
            self.conn = self.listener.accept()
            self.logger.log("Connection accepted from %s:%d" %
                            self.listener.last_accepted)
        except KeyboardInterrupt:
            print("Exiting")
            self.terminate = True

    def reply(self, payload):
        """Send the single reply this request is waiting for.

        Tracked so that the error path in listen() can tell whether the client
        is still waiting, and does not queue a second reply that would leave
        every subsequent recv() off by one.
        """
        self.replied = True
        self.conn.send(payload)

    def handle_msg(self, item):
        if item.data == SERVER_EXIT:
            self.logger.log("* Received exit signal *")
            self.reply(SERVER_ACK)
            self.close()
        elif item.data == SERVER_DATA_REQ:
            self.logger.log("* Recieved data request")
            item = self.get_data_msg()
            if item:
                self.reply(item.data)
            else:
                self.reply(None)
        else:
            # Forward the message to the device
            self.send_data(item.data)

            # Reply with acknowledgement
            self.reply(SERVER_ACK)

    def send_data(self, data):
        # `with` rather than acquire/release so an exception cannot leak the
        # lock and starve the DeviceScanner thread.
        with self.serial_cv:
            self.ser.send_and_confirm(data, False)

    # Wait for and handle incoming connections
    def listen(self):
        self.logger.log("Server started")
        self.get_connection()

        while not self.terminate:
            data = None
            try:
                data = self.conn.recv()
                self.replied = False
                item = InputItem.from_data(data)

                self.logger.log("Received: %s" % item)
                self.handle_msg(item)
                self.logger.log("Acked: %s" % item)

            except (EOFError, IOError):
                # Attempt to reconnect
                self.logger.log("Lost connection")
                self.listener.close()
                self.get_connection()
            except Exception:
                # A malformed or unexpected message must not take down the
                # server -- log it, drop the message, and keep serving.
                self.logger.log("Exception handling message:\n%s" %
                                traceback.format_exc())
                if not self.replied:
                    # The client is still waiting, so unblock it.  Only when we
                    # have not already replied: a second reply would desync the
                    # stream and be read as the next request's response.
                    #
                    # The reply must match the shape the client expects for
                    # this request.  A data request is awaiting serial payload
                    # or None; handing it the plain-str ack would be treated as
                    # payload and blow up in hexlify()/decode_data().
                    recovery = None if data == SERVER_DATA_REQ else SERVER_ACK
                    try:
                        self.reply(recovery)
                    except Exception:
                        pass

    def close(self):
        self.listener.close()
        if self.conn:
            self.conn.close()
        self.terminate = True

    def get_data_msg(self, timeout=None):
        """Listen on the serial device for a properly formatted data message"""

        if timeout is None:
            timeout = self.DATA_TIMEOUT

        # `with` rather than acquire/release: decode_data() below parses
        # attacker-shaped serial input and can raise, and a leaked lock would
        # permanently starve the DeviceScanner thread.
        with self.serial_cv:
            self.logger.log("Starting data request")

            time_limit = time.time() + timeout
            item = None
            while True:
                # Try to get a message with low timeout
                item = self.ser.get_message(timeout=0.1)
                if item and item.is_hmtl:
                    self.logger.log("Received response: %s:\n%s" %
                                    (item, HMTLprotocol.decode_data(item.data)))
                    break

                # Check for timeout
                if time.time() > time_limit:
                    self.logger.log("Data request time limit exceeded")
                    item = None
                    break

        return item


class DeviceScanner(threading.Thread):
    """
    This class performs a background scan for HMTL devices and maintains a list
    of discovered devices
    """

    def __init__(self, server, verbose=True, period=60.0):
        threading.Thread.__init__(self)

        self.server = server
        self.verbose = verbose

        self.logger = TimedLogger(self.server.ser.serial.start_time,
                                  textcolor=TimedLogger.MAGENTA)
        self.logger.log("Scanner initialized")

        # Period between scans
        self.scan_period = period

        # Period between address
        self.address_period = 0.25

        # TODO: This range is arbitrary for scanning purposes
        self.address_range = [x for x in range(120, 150)]

        # Set as a daemon so that this thread will exit correctly
        # when the parent receives a kill signal
        self.daemon = True

        self.devices = {}

    def get_devices(self):
        return self.devices

    def log(self, msg):
        if self.verbose:
            self.logger.log(msg)

    def stop(self):
        pass

    def run(self):
        self.log("Scanner started")

        while True:
            self.log("Starting scan")

            for address in self.address_range:
                self.log("Polling address %d" % address)

                msg = HMTLprotocol.get_poll_msg(address)

                try:
                    self.server.send_data(msg)
                    # Explicit short timeout: most scanned addresses are empty,
                    # and the scan should not inherit the RS485-sized
                    # DATA_TIMEOUT once per dead address.
                    item = self.server.get_data_msg(
                        timeout=HMTLServer.SCAN_TIMEOUT)
                    if item:
                        (text, msg) = HMTLprotocol.decode_msg(item.data)
                        if (isinstance(msg, HMTLprotocol.PollHdr)):
                            self.log("Poll response: %s" % (msg.dump()))

                            if self.devices.get(address):
                                # A device previously responded to this address
                                self.devices[address].update(msg)
                            else:
                                # Create a new device on this address
                                self.devices[address] = HMTLModule(msg)
                        else:
                            self.log("XXX: Wrong message type? %s" % str(msg))
                    elif self.devices.get(address):
                        # There was no response for a module we previously had configured
                        self.log("No response for known address %d" % address)
                        self.devices[address].set_active(False)
                except Exception as e:
                    print("Exception: %s" % e)
                    pass

                time.sleep(self.address_period)

            self.log("Current devices:")
            for deviceid in self.devices.keys():
                self.log("  %s" % (self.devices[deviceid].dump()))

            time.sleep(self.scan_period)


class HMTLModule(object):

    def __init__(self, pollhdr):
        self.protocol_version = pollhdr.protocol_version
        self.hardware_version = pollhdr.hardware_version
        self.baud = pollhdr.baud
        self.num_outputs = pollhdr.num_outputs
        self.flags = pollhdr.flags
        self.device_id = pollhdr.device_id
        self.address = pollhdr.address

        self.object_type = pollhdr.object_type
        self.buffer_size = pollhdr.buffer_size
        self.msg_version = pollhdr.msg_version

        self.active = True
        self.last_active = time.time()

    def update(self, pollhdr):
        """
        Update an existing module based on a poll header
        :param pollhdr:
        :return:
        """
        self.set_active(True)
        pass

    def set_active(self, active):
        self.active = active
        if self.active:
            self.last_active = time.time()
