#!/usr/bin/env python3
"""Compile package translation units to Extreme's ILP32 LLVM bitcode inputs."""

import os
from pathlib import Path
import subprocess
import re
import sys

llvm = Path(os.environ.get("CPPHDL_HOME") or str(Path.home() / "cpphdl")) / ".conda/bin"
args = sys.argv[1:]
# Package builds consume bitcode objects and LLVM archives. Machine code is emitted
# only by extreme-codegen after linking with the native kernel platform.
if "-c" not in args and "-E" not in args and "-S" not in args:
    raise SystemExit(
        "Extreme package wrapper only compiles objects; use ld.py to link bitcode"
    )
flags = [
    "--target=armv7-none-eabi",
    "-mfloat-abi=soft",
    "-O2",
    "-ffreestanding",
    "-fno-vectorize",
    "-fno-slp-vectorize",
    "-fno-unroll-loops",
    "-fno-stack-protector",
    "-D__EXTREME__=1",
    "-DEC_BUS_BYTES=" + os.environ.get("EXTREME_BUS_BYTES", "64"),
]
if "-E" not in args:
    flags += ["-emit-llvm"]
sources = [Path(arg) for arg in args if arg.endswith((".S", ".s"))]
if sources and "-c" in args:
    output_index = args.index("-o")
    preprocess = [
        arg
        for i, arg in enumerate(args)
        if i not in (output_index, output_index + 1) and arg != "-c"
    ]
    result = subprocess.run(
        [
            str(llvm / "clang"),
            *[f for f in flags if f != "-emit-llvm"],
            "-E",
            "-P",
            *preprocess,
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    if any(
        line.strip() and not line.lstrip().startswith("#")
        for line in result.stdout.splitlines()
    ):
        raise SystemExit("Extreme does not accept assembly: " + str(sources[0]))
    # Architecture-specific assembly guarded out by preprocessing contributes
    # an empty IR module, never an ARM object in an Extreme archive.
    args = [arg for arg in args if arg not in [str(p) for p in sources]]
    args += ["-x", "c", "/dev/null"]
result = subprocess.run([str(llvm / "clang"), *flags, *args])
if result.returncode:
    raise SystemExit(result.returncode)
if "-c" in args and "-o" in args:
    output = Path(args[args.index("-o") + 1])
    ir = subprocess.check_output(
        [str(llvm / "llvm-dis"), str(output), "-o", "-"], text=True
    )
    lines = []
    for line in ir.splitlines():
        if (
            line.startswith("@")
            and re.search(r"\bglobal\b", line)
            and " external " not in line
        ):
            if "section " not in line:
                line = re.sub(
                    r", align (\d+)", r', section ".extreme.user", align \1', line
                )
                if 'section ".extreme.user"' not in line:
                    line += ', section ".extreme.user"'
        lines.append(line)
    subprocess.run(
        [str(llvm / "llvm-as"), "-o", str(output)],
        input="\n".join(lines) + "\n",
        text=True,
        check=True,
    )
