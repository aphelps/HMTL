#!/usr/bin/env python3
"""Prove the cross-ABI layout asserts can fail.

A static_assert that is true by construction, or that lives in a file no
toolchain compiles, is not a guard — it is a comment that costs a build step.
This subsystem has shipped several of those, so every assert here is perturbed
by one and the build must go red.

For each asserted number, in each header, under each available toolchain:
copy the sources to a scratch tree, add 1 to the number, compile, and require
failure. The originals are never modified.

    ./negative_control.py            # every toolchain the Makefile knows about
    ./negative_control.py --list     # show what would be perturbed, compile nothing

Exit status is 0 only if every assert failed when broken AND the unmodified
tree compiles clean everywhere.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
HMTL_ROOT = HERE.parent.parent
LIBS = HMTL_ROOT / "Libraries"
STUBS = HMTL_ROOT / "platformio/HMTL_Test/stubs"
PIO_PKG = Path(os.environ.get("PIO_PKG", Path.home() / ".platformio/packages"))

# Files whose asserts this script is responsible for. Paths are relative to
# Libraries/, except the two under tests/layout/ which are named absolutely.
#
# HMTLWireFormat.h itself carries no static_asserts — HMTL#6 put its per-struct
# numbers in a test file — so listing it here alone would have been a control
# that controlled nothing. Those numbers now live in wire_layout_asserts.h,
# which is where this reaches them.
GUARDED = [
    LIBS / "HMTLMessaging/HMTLPrograms.h",
    LIBS / "TimeSync/TimeSync.h",
    HERE / "wire_layout_asserts.h",
]

# Lines carrying an assertion. Anything matching is expected to break the build
# when its number changes.
ASSERT_RE = re.compile(
    r"HMTL_LAYOUT_SIZE\(|HMTL_LAYOUT_OFF\(|WF_SIZE\(|WF_OFF\(|static_assert\("
)

# Both settings of the flag. It was added for hmtl_program_color_t, which used
# to embed pixel_range_t and so had a flag-dependent wire layout; that struct is
# now fixed-width and identical under both settings. The asserts that still
# differ per variant are the pixel_range_t pair in HMTLPrograms.h — 2 bytes by
# default, 4 under -DBIG_PIXELS — each compiled by only one half of this axis,
# which is why a perturbation counts as caught if ANY variant goes red.
PIXEL_VARIANTS = [("default", []), ("BIG_PIXELS", ["-DBIG_PIXELS"])]

BASE_FLAGS = [
    "-fsyntax-only", "-std=c++11", "-include", "Arduino.h",
    "-DDEBUG_LEVEL=0", "-DDISABLE_RS485", "-DDISABLE_XBEE", "-DDISABLE_MPR121",
]


def toolchains():
    """(name, argv-prefix) for each compiler present on this machine."""
    found = [
        ("host", ["g++"]),
        ("host-avr-proxy", ["g++", "-fpack-struct=1"]),
    ]
    avr = PIO_PKG / "toolchain-atmelavr/bin/avr-g++"
    if avr.is_file() and os.access(avr, os.X_OK):
        found.append(("avr", [str(avr), "-mmcu=atmega328p"]))
    else:
        print(f"note: avr-g++ not found at {avr}, skipping that ABI")
    xt = PIO_PKG / "toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++"
    if xt.is_file() and os.access(xt, os.X_OK):
        found.append(("xtensa", [str(xt)]))
    else:
        print(f"note: xtensa-esp32-elf-g++ not found at {xt}, skipping that ABI")
    return found


def compile_tree(cxx, tree, variant_flags):
    """Compile layout_check.cpp against a scratch source tree.

    `tree` holds a copy of both Libraries/ and this directory, so a perturbed
    wire_layout_asserts.h is picked up too.
    """
    libs_dir = tree / "Libraries"
    layout = tree / "layout"
    includes = [
        f"-I{libs_dir}/TimeSync", f"-I{libs_dir}/HMTLprotocol",
        f"-I{libs_dir}/HMTLMessaging", f"-I{libs_dir}/HMTLTypes", f"-I{STUBS}",
        f"-I{layout}",
    ]
    return subprocess.run(
        cxx + BASE_FLAGS + includes + variant_flags + [str(layout / "layout_check.cpp")],
        cwd=HERE, capture_output=True, text=True,
    )


def bump_last_code_int(text):
    """Add 1 to the last integer literal in code on this line.

    Digits inside a // comment or a string literal are not part of the assert.
    Bumping one of those yields a mutation that correctly does NOT fail the
    build, which would be misreported as an assert that cannot fail.
    """
    masked = list(text)
    in_str = False
    i = 0
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
            masked[i] = " "
        elif c == '"':
            in_str = True
            masked[i] = " "
        elif c == "/" and text[i:i + 2] == "//":
            masked[i:] = " " * (len(text) - i)
            break
        elif c == "/" and text[i:i + 2] == "/*":
            end = text.find("*/", i + 2)
            end = len(text) if end == -1 else end + 2
            for j in range(i, end):
                masked[j] = " "
            i = end
            continue
        i += 1
    matches = list(re.finditer(r"\d+", "".join(masked)))
    if not matches:
        return None
    m = matches[-1]
    return text[:m.start()] + str(int(m.group()) + 1) + text[m.end():]


def assert_lines(path):
    out = []
    for i, line in enumerate(path.read_text().splitlines(keepends=True)):
        if ASSERT_RE.search(line) and "#define" not in line:
            mutated = bump_last_code_int(line)
            if mutated and mutated != line:
                out.append((i, line, mutated))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", action="store_true",
                    help="print the asserts that would be perturbed, compile nothing")
    args = ap.parse_args()

    if args.list:
        for path in GUARDED:
            for i, line, _ in assert_lines(path):
                print(f"{path.name}:{i + 1}: {line.strip()}")
        return 0

    chains = toolchains()
    scratch = Path(tempfile.mkdtemp(prefix="hmtl-layout-"))
    try:
        # Copy both trees: an assert may live in Libraries/ or in this directory.
        shutil.copytree(LIBS, scratch / "Libraries")
        shutil.copytree(HERE, scratch / "layout")

        def scratch_path(src):
            if LIBS in src.parents:
                return scratch / "Libraries" / src.relative_to(LIBS)
            return scratch / "layout" / src.relative_to(HERE)

        # A negative control means nothing if the baseline is already red.
        for name, cxx in chains:
            for vname, vflags in PIXEL_VARIANTS:
                res = compile_tree(cxx, scratch, vflags)
                if res.returncode != 0:
                    print(f"BASELINE FAILS under {name} [{vname}]:\n{res.stderr}")
                    return 1
        combos = [f"{n}[{v}]" for n, _ in chains for v, _ in PIXEL_VARIANTS]
        print(f"baseline compiles clean under: {', '.join(combos)}")

        total = survived = 0
        for src in GUARDED:
            orig = src.read_text()
            target = scratch_path(src)
            for i, line, mutated in assert_lines(src):
                lines = orig.splitlines(keepends=True)
                lines[i] = mutated
                target.write_text("".join(lines))
                # One assert may only be reachable under one flag setting (the
                # colour struct is the reason this axis exists), so a
                # perturbation counts as caught if ANY variant goes red.
                for name, cxx in chains:
                    total += 1
                    caught = any(
                        compile_tree(cxx, scratch, vflags).returncode != 0
                        for _, vflags in PIXEL_VARIANTS
                    )
                    if not caught:
                        survived += 1
                        print(f"!! {name}: no failure under any pixel variant when "
                              f"breaking {src.name}:{i + 1}: {line.strip()}")
                target.write_text(orig)

        print(f"\n{total} perturbations across {len(chains)} toolchains "
              f"x {len(PIXEL_VARIANTS)} pixel-width variants: "
              f"{total - survived} broke the build as required, {survived} did not")
        return 1 if survived else 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
