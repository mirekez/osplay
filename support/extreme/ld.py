#!/usr/bin/env python3
"""Combine package relocatable objects without emitting foreign machine code."""

import os
from pathlib import Path
import subprocess
import sys

llvm = Path(os.environ.get("CPPHDL_HOME") or str(Path.home() / "cpphdl")) / ".conda/bin"
args = sys.argv[1:]
output = args[args.index("-o") + 1]
inputs = [arg for arg in args if arg != output and arg.endswith((".o", ".a", ".bc"))]
raise SystemExit(subprocess.call([str(llvm / "llvm-link"), *inputs, "-o", output]))
