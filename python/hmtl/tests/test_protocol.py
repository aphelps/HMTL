"""Wire-layout tests for the typed program encoders in HMTLprotocol.

This is the first test in this package that packs a program message, which is
worth saying plainly: ProgramFade, ProgramSparkle and ProgramCircular have
never had one, and hmtl_program_color_t drifted into having two incompatible
wire layouts precisely because nothing on either side of the bus pinned it.

Every expectation below is a LITERAL byte string, transcribed by hand from the
C struct. Deriving it from FORMAT would only prove FORMAT agrees with itself,
which is the failure this file exists to avoid — the C side asserts the same
numbers independently in Libraries/HMTLMessaging/HMTLPrograms.h and
platformio/HMTL_Test/test/test_pixel_programs/, so the two implementations
check each other rather than a shared source of truth.

Run:
    cd python
    pytest hmtl/tests/test_protocol.py -v
"""

import os
import socket
import subprocess
import sys

import pytest

import hmtl.HMTLprotocol as HMTLprotocol

# Repo-relative, and PYTHONPATH is set explicitly when invoking the CLI below.
# Not defensive tidiness: a hand-run of `bin/HMTLClient` during this work
# imported an INSTALLED hmtl package instead of the checkout and reported
# "no attribute 'ProgramColor'" — a CLI check proves nothing until you know
# which copy of the library it loaded.
_PYTHON_DIR = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))
_CLI = os.path.join(_PYTHON_DIR, "bin", "HMTLClient")


def _closed_port():
    """A port nothing is listening on: bind, read the number, release it."""
    s = socket.socket()
    try:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]
    finally:
        s.close()


def _run_cli(*args):
    """Run the CLI against a deliberately dead command server, with a timeout.

    Both of those are load-bearing, and neither was here first time round.

    The DEAD PORT: HMTLClient's default target is localhost:6000, and any
    argument list that gets past validation falls through to connecting there
    and transmitting. A test that reached that path passed only because nothing
    happened to be listening; with a real HMTL command server up on the
    developer's machine it would have put a live broadcast COLOR on the fleet.

    The TIMEOUT: neither `multiprocessing.connection.Client` nor
    `send_and_ack`'s `while True` has one, so a socket that accepts and then
    says nothing hangs `make test` forever. A hang has to read as a failure.
    """
    env = dict(os.environ, PYTHONPATH=_PYTHON_DIR)
    return subprocess.run(
        [sys.executable, _CLI, "-p", str(_closed_port()), *args],
        capture_output=True, text=True, env=env, timeout=30)


# ---------------------------------------------------------------------------
# ProgramColor — hmtl_program_color_t
#
#   typedef struct __attribute__((__packed__)) {
#     CRGB color;                  // 3 B: r, g, b
#     wire_pixel_range_t range;    // 4 B: uint16_t start, uint16_t length
#   } hmtl_program_color_t;        // 7 B, range at offset 3
#
# then zero-padded to ProgramHdr.MAX_DATA (32) because the firmware reads a
# fixed-size values[] array.
# ---------------------------------------------------------------------------

def test_color_payload_is_the_transcribed_seven_bytes():
    packed = HMTLprotocol.ProgramColor([0x11, 0x22, 0x33], start=0x0102,
                                       length=0x0304).pack()

    assert packed[:7] == b"\x11\x22\x33\x02\x01\x04\x03"
    assert len(packed) == HMTLprotocol.ProgramHdr.MAX_DATA
    assert packed[7:] == b"\x00" * (HMTLprotocol.ProgramHdr.MAX_DATA - 7)


def test_color_range_is_little_endian_and_not_byte_wide():
    """A range above 255 must occupy two bytes, not truncate into one.

    This is the property the widening bought: -DBIG_PIXELS builds drive strips
    longer than 255 pixels, and the old layout could not address them from a
    default-flag sender at all.
    """
    packed = HMTLprotocol.ProgramColor([0, 0, 0], start=300, length=400).pack()

    # 300 = 0x012c, 400 = 0x0190, little-endian.
    assert packed[3:7] == b"\x2c\x01\x90\x01"


def test_color_program_number_is_0x31():
    assert HMTLprotocol.ProgramColor.TYPE_NUM == 0x31
    assert HMTLprotocol.ProgramGeneric.NAME_MAP["color"] == 0x31


def test_color_message_carries_the_payload_after_the_headers():
    msg = HMTLprotocol.ProgramColor([0xff, 0x00, 0x00], start=2,
                                    length=3).prepare_msg(address=5, output=1)

    # Literal, not `ProgramColor(...).pack()` — deriving the expectation from
    # the encoder under test is the one thing this file is trying not to do.
    assert msg[-32:] == b"\xff\x00\x00\x02\x00\x03\x00" + b"\x00" * 25
    assert len(msg) == (HMTLprotocol.MsgHdr.LENGTH +
                        HMTLprotocol.ProgramHdr.LENGTH)
    # The program type byte sits immediately before the payload.
    assert msg[-33] == 0x31


# ---------------------------------------------------------------------------
# The validity rule
#
# The firmware rejects length == 0 with a nonzero start (program_color in
# HMTLPrograms.cpp, and test_color_zero_length_with_nonzero_start_is_rejected
# in the native suite). These pin the same rule on this side.
#
# This is the half that DRIFTED. The rule was added to the C reader in one
# round and not to the encoder, so for one commit this class's docstring
# asserted the opposite and pack() happily produced messages the module drops
# — a two-implementation cross-check disagreeing about validity instead of
# layout, which is the same class of bug the struct had.
# ---------------------------------------------------------------------------

def test_zero_length_with_nonzero_start_is_refused_by_the_encoder():
    with pytest.raises(ValueError) as excinfo:
        HMTLprotocol.ProgramColor([1, 2, 3], start=5, length=0)
    # The message has to say what to do instead, not just that it failed.
    assert "start must be 0" in str(excinfo.value)


def test_zero_length_with_zero_start_is_the_whole_strip_and_is_allowed():
    """The one zero-length form that must survive.

    It is what this class's own zero-fill produces for a colour program given
    nothing but an RGB triple, and it means the same thing in the old and new
    layouts — which is why the migration is small.
    """
    packed = HMTLprotocol.ProgramColor([0, 0, 0xff], start=0, length=0).pack()
    assert packed[:7] == b"\x00\x00\xff\x00\x00\x00\x00"


def test_nonzero_start_with_a_real_length_is_unaffected():
    """The rule constrains only the zero-length case."""
    packed = HMTLprotocol.ProgramColor([1, 2, 3], start=5, length=1).pack()
    assert packed[:7] == b"\x01\x02\x03\x05\x00\x01\x00"


# ---------------------------------------------------------------------------
# The migration hazard, pinned rather than described
# ---------------------------------------------------------------------------

def test_stale_generic_invocation_now_encodes_a_rejectable_pairing():
    """`HMTLClient -P color -C 255,0,0,0,10` used to mean start=0, length=10.

    Read at the current layout those same five bytes are start=2560, length=0.
    That is the whole family, not one example: five bytes always leave wire
    bytes 5-6 zero, so the wire length is ALWAYS 0 and the old start lands in
    the high half of the new one. The firmware refuses zero length with a
    nonzero start for exactly this reason (program_color in HMTLPrograms.cpp,
    and test_color_stale_five_byte_invocation_is_rejected_not_flooded in the
    native suite). This test pins the encoding half so the two sides cannot
    drift apart again.
    """
    stale = HMTLprotocol.ProgramGeneric([255, 0, 0, 0, 10]).pack()

    assert stale[:5] == b"\xff\x00\x00\x00\x0a"

    # Decoded as the current struct: start is bytes 3-4 LE, length bytes 5-6.
    start = stale[3] | (stale[4] << 8)
    length = stale[5] | (stale[6] << 8)
    assert start == 2560
    assert length == 0


def test_typed_and_generic_forms_agree_when_the_generic_one_is_written_out():
    """The typed class is not a new layout — it is the same bytes, named.

    Hand-assembling the seven little-endian bytes through ProgramGeneric must
    produce byte-identical output to ProgramColor, which is what makes --color
    a convenience rather than a second encoding to keep in sync.
    """
    typed = HMTLprotocol.ProgramColor([1, 2, 3], start=513, length=1027).pack()
    generic = HMTLprotocol.ProgramGeneric(
        [1, 2, 3, 513 & 0xff, 513 >> 8, 1027 & 0xff, 1027 >> 8]).pack()

    assert typed == generic


# ---------------------------------------------------------------------------
# The CLI refusal path
#
# HMTLClient constructs before it announces, and range-checks before it
# constructs. Both orderings shipped as bug fixes and neither was pinned:
# printing first meant a refused message still reported "Sending ... (whole
# strip)", and one try around the int() conversions meant a typo reported that
# the module would drop a message that had never been built.
# ---------------------------------------------------------------------------

def test_cli_refuses_the_invalid_pairing_without_claiming_to_send():
    r = _run_cli("--color", "-C", "255,0,0,5,0")

    assert r.returncode == 1
    assert "start must be 0" in r.stdout
    # The ordering, not just the refusal: nothing may announce a send.
    assert "Sending COLOR message" not in r.stdout


def test_cli_reports_a_non_integer_as_a_parse_error_not_a_module_rejection():
    r = _run_cli("--color", "-C", "255,0,x")

    assert r.returncode == 1
    assert "must be integers" in r.stdout
    # The message that would be wrong here — nothing was built to be dropped.
    assert "start must be 0" not in r.stdout
    assert "invalid literal" not in r.stdout


def test_cli_reports_an_out_of_range_field_rather_than_a_struct_traceback():
    """struct.error is not a ValueError, so an unchecked field escaped raw."""
    r = _run_cli("--color", "-C", "255,0,0,70000,5")

    assert r.returncode == 1
    assert "0-65535" in r.stdout
    assert "Traceback" not in r.stderr


def test_cli_accepts_a_valid_range():
    """The only one of these that reaches the accept path — mind what that means.

    Past validation the CLI tries to reach a command server, so this asserts on
    what it PRINTED before that, not on the exit code: the connection to the
    dead port fails, giving the same status 1 the refusal tests use as evidence
    of rejection. Distinguishing them by returncode would be reading a number
    that means two different things.

    The refusal tests never get here — they `sys.exit(1)` well before the
    HMTLClient construction — which is why only this one needed the dead port.
    """
    r = _run_cli("--color", "-C", "255,0,0,5,3")

    assert "Sending COLOR message" in r.stdout
    assert "start=5 length=3" in r.stdout
    # It got past validation rather than being refused for some other reason.
    assert "must be" not in r.stdout
