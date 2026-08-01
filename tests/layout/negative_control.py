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

# Headers whose asserts this script is responsible for, relative to Libraries/.
GUARDED = [
    "HMTLMessaging/HMTLPrograms.h",
    "TimeSync/TimeSync.h",
    "HMTLprotocol/HMTLWireFormat.h",
]

# Lines carrying an assertion. Anything matching is expected to break the build
# when its number changes.
ASSERT_RE = re.compile(
    r"HMTL_LAYOUT_SIZE\(|HMTL_LAYOUT_OFF\(|static_assert\("
)

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


def compile_tree(cxx, libs_dir):
    includes = [
        f"-I{libs_dir}/TimeSync", f"-I{libs_dir}/HMTLprotocol",
        f"-I{libs_dir}/HMTLMessaging", f"-I{libs_dir}/HMTLTypes", f"-I{STUBS}",
    ]
    return subprocess.run(
        cxx + BASE_FLAGS + includes + ["layout_check.cpp"],
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
        for rel in GUARDED:
            for i, line, _ in assert_lines(LIBS / rel):
                print(f"{rel}:{i + 1}: {line.strip()}")
        return 0

    chains = toolchains()
    scratch = Path(tempfile.mkdtemp(prefix="hmtl-layout-"))
    try:
        libs_copy = scratch / "Libraries"
        shutil.copytree(LIBS, libs_copy)

        # A negative control means nothing if the baseline is already red.
        for name, cxx in chains:
            res = compile_tree(cxx, libs_copy)
            if res.returncode != 0:
                print(f"BASELINE FAILS under {name}:\n{res.stderr}")
                return 1
        print(f"baseline compiles clean under: {', '.join(n for n, _ in chains)}")

        total = survived = 0
        for rel in GUARDED:
            orig = (LIBS / rel).read_text()
            target = libs_copy / rel
            for i, line, mutated in assert_lines(LIBS / rel):
                lines = orig.splitlines(keepends=True)
                lines[i] = mutated
                target.write_text("".join(lines))
                for name, cxx in chains:
                    total += 1
                    if compile_tree(cxx, libs_copy).returncode == 0:
                        survived += 1
                        print(f"!! {name}: no failure when breaking "
                              f"{rel}:{i + 1}: {line.strip()}")
                target.write_text(orig)

        print(f"\n{total} perturbations across {len(chains)} toolchains: "
              f"{total - survived} broke the build as required, {survived} did not")
        return 1 if survived else 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
