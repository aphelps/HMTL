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

import hmtl.HMTLprotocol as HMTLprotocol


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
# The migration hazard, pinned rather than described
# ---------------------------------------------------------------------------

def test_stale_generic_invocation_now_encodes_a_rejectable_start():
    """`HMTLClient -P color -C 255,0,0,0,10` used to mean start=0, length=10.

    Read at the current layout those same five bytes are start=2560,
    length=0 — and length 0 means the whole strip. The firmware refuses a start
    past the end for exactly this reason (see program_color in
    HMTLPrograms.cpp, and test_color_stale_five_byte_invocation_is_rejected in
    the native suite). This test pins the encoding half of that story so the
    two sides cannot drift apart again.
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
