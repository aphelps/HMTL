"""Regression tests for the client/server python3 fixes.

Each test here pins a specific bug that reached hardware.  All of them are pure
host-side Python -- no device, no serial port, no network.

Run:
    cd python
    pytest hmtl/tests/test_client_server_regressions.py -v
"""

import struct
import threading

import pytest

import hmtl.HMTLprotocol as HMTLprotocol
import hmtl.server as server
from hmtl.server import HMTLServer


# ---------------------------------------------------------------------------
# DumpConfigHdr.from_data: py2 ord() on a bytes object
# ---------------------------------------------------------------------------

def _config_header_bytes():
    """A config_hdr_t payload whose first byte is HEADER_MAGIC."""
    return struct.pack(
        HMTLprotocol.ConfigHeaderMain.FORMAT,
        HMTLprotocol.HEADER_MAGIC,
        3,      # protocol_version
        5,      # hardware_version
        8,      # baud (9600 / 1200)
        7,      # num_outputs
        0,      # flags
        71,     # device_id
        71,     # address
    )


def test_from_data_accepts_bytes_with_header_magic():
    """Regression: ord(data[0]) raised TypeError on python3 bytes.

    This is the exact failure that propagated out of the server's listen loop
    and killed HMTLCommandServer whenever a client ran `--dump`.
    """
    data = _config_header_bytes()

    hdr = HMTLprotocol.DumpConfigHdr.from_data(data)

    assert hdr is not None
    # Routed to the config-header branch, not full_config()
    assert isinstance(hdr.config, HMTLprotocol.ConfigHeaderMain)
    assert hdr.config.device_id == 71
    assert hdr.config.address == 71


def test_from_data_honours_offset():
    """Regression: `offset` was accepted but ignored.

    A nonzero offset silently tested the wrong byte and misrouted the payload
    to full_config().
    """
    padding = b"\xde\xad\xbe\xef"
    data = padding + _config_header_bytes()

    hdr = HMTLprotocol.DumpConfigHdr.from_data(data, offset=len(padding))

    assert isinstance(hdr.config, HMTLprotocol.ConfigHeaderMain)
    assert hdr.config.device_id == 71


# ---------------------------------------------------------------------------
# Server listen loop: survive a bad message, and reply exactly once
# ---------------------------------------------------------------------------

class _FakeConn:
    """Minimal stand-in for a multiprocessing Connection.

    Yields a scripted sequence of received payloads, then raises EOFError to
    break the server out of its loop.  Everything sent back is recorded.
    """

    def __init__(self, incoming):
        self.incoming = list(incoming)
        self.sent = []
        self.closed = False

    def recv(self):
        if not self.incoming:
            raise EOFError
        return self.incoming.pop(0)

    def send(self, payload):
        self.sent.append(payload)

    def close(self):
        self.closed = True


def _bare_server():
    """An HMTLServer with just the attributes listen()/handle_msg() touch."""
    srv = HMTLServer.__new__(HMTLServer)
    srv.terminate = False
    srv.replied = False
    srv.serial_cv = threading.Condition()
    srv.scanner = None
    srv.conn = None
    srv.listener = None

    class _NullLogger:
        def log(self, *args, **kwargs):
            pass

    srv.logger = _NullLogger()
    return srv


def test_listen_survives_a_failing_message_and_acks_once(monkeypatch):
    """Regression: one bad message used to terminate the whole server.

    It must now log, reply exactly once so the client is not left hanging, and
    stay in the loop to serve the next request.
    """
    srv = _bare_server()
    handled = []

    def _boom(item):
        handled.append(item)
        raise ValueError("malformed response")

    srv.handle_msg = _boom
    # listen() reconnects on EOFError; make that terminate the loop instead.
    # listen() calls get_connection() once up front, then again on EOFError.
    # Let the first call through; make the reconnect end the loop.
    calls = []

    def _get_connection():
        calls.append(1)
        if len(calls) > 1:
            srv.terminate = True

    srv.get_connection = _get_connection
    srv.listener = type("_L", (), {"close": lambda self: None})()

    conn = _FakeConn([b"first", b"second"])
    srv.conn = conn

    srv.listen()

    # Both messages were attempted -- the server did not die on the first.
    assert len(handled) == 2
    # Exactly one recovery ack per failed message, and nothing else.
    assert conn.sent == [server.SERVER_ACK, server.SERVER_ACK]


def test_no_second_reply_when_handler_already_replied():
    """Regression: the recovery path could queue a duplicate ack.

    handle_msg() replies in every branch, so an exception raised *after* that
    reply must not produce a second one -- it would be read as the response to
    the client's next request.
    """
    srv = _bare_server()

    def _reply_then_fail(item):
        srv.reply(server.SERVER_ACK)
        raise RuntimeError("failure after the reply was already sent")

    srv.handle_msg = _reply_then_fail
    # listen() calls get_connection() once up front, then again on EOFError.
    # Let the first call through; make the reconnect end the loop.
    calls = []

    def _get_connection():
        calls.append(1)
        if len(calls) > 1:
            srv.terminate = True

    srv.get_connection = _get_connection
    srv.listener = type("_L", (), {"close": lambda self: None})()

    conn = _FakeConn([b"only"])
    srv.conn = conn

    srv.listen()

    # Exactly one reply, not two.
    assert conn.sent == [server.SERVER_ACK]


def test_get_data_msg_releases_lock_when_decode_raises(monkeypatch):
    """Regression: a raising decode_data() leaked serial_cv.

    Harmless for the single-threaded listen loop (Condition wraps an RLock, so
    it re-enters), but it permanently starved the DeviceScanner thread.
    """
    srv = _bare_server()

    class _Item:
        is_hmtl = True
        data = b"\xfc\x00garbage"

    srv.ser = type("_S", (), {"get_message": lambda self, timeout: _Item()})()

    def _explode(_data):
        raise ValueError("undecodable")

    monkeypatch.setattr(HMTLprotocol, "decode_data", _explode)

    with pytest.raises(ValueError):
        srv.get_data_msg(timeout=0.05)

    # The lock must be free for *another* thread, which is what the scanner is.
    acquired = []

    def _try_acquire():
        acquired.append(srv.serial_cv.acquire(blocking=False))
        if acquired[0]:
            srv.serial_cv.release()

    t = threading.Thread(target=_try_acquire)
    t.start()
    t.join()

    assert acquired == [True], "serial_cv was leaked; scanner thread would starve"


def test_scan_timeout_is_shorter_than_data_timeout():
    """The address sweep must not inherit the RS485-sized response timeout."""
    assert HMTLServer.SCAN_TIMEOUT < HMTLServer.DATA_TIMEOUT


def test_failed_data_request_recovers_with_none_not_ack():
    """Regression: the recovery reply must match the request's expected shape.

    A data request is awaiting serial payload (bytes) or None.  Replying with
    the plain-str SERVER_ACK put a str where the client expects bytes, and a
    verbose client then died in hexlify() -- the same crash class this branch
    set out to remove.
    """
    srv = _bare_server()

    def _boom(item):
        raise ValueError("decode blew up inside get_data_msg")

    srv.handle_msg = _boom

    calls = []

    def _get_connection():
        calls.append(1)
        if len(calls) > 1:
            srv.terminate = True

    srv.get_connection = _get_connection
    srv.listener = type("_L", (), {"close": lambda self: None})()

    conn = _FakeConn([server.SERVER_DATA_REQ])
    srv.conn = conn

    srv.listen()

    assert conn.sent == [None], (
        "a failed data request must recover with None, not the str ack")


# ---------------------------------------------------------------------------
# Client: verbose ack logging
# ---------------------------------------------------------------------------

def test_get_ack_verbose_does_not_hexlify_the_str_ack():
    """Regression: get_ack() hexlified the ack, which is a plain str.

    Every --verbose run died with TypeError before printing anything.
    """
    from hmtl.client import HMTLClient

    client = HMTLClient.__new__(HMTLClient)
    client.verbose = True
    client.conn = type("_C", (), {"recv": lambda self: server.SERVER_ACK})()

    logged = []
    client.logger = type(
        "_L", (), {"log": lambda self, msg, **kw: logged.append(msg)})()

    # Must not raise, and must correctly recognise the ack.
    assert client.get_ack() is True
    assert logged, "verbose mode should have logged the received ack"
