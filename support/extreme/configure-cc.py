#!/usr/bin/env python3
"""Autoconf link probes against the actual Extreme bitcode libc interfaces."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent
base = Path(
    os.environ.get("EXTREME_BUILD_ROOT", str(root.parents[1] / "build/tests/extreme"))
)
llvm = Path(os.environ.get("CPPHDL_HOME") or str(Path.home() / "cpphdl")) / ".conda/bin"
args = sys.argv[1:]
if any(arg in args for arg in ("-c", "-E", "-S")):
    raise SystemExit(subprocess.call([str(root / "cc.py"), *args]))
if any(arg in args for arg in ("--version", "-v", "-V", "-qversion", "-version")):
    raise SystemExit(subprocess.call([str(llvm / "clang"), *args]))
output = args[args.index("-o") + 1] if "-o" in args else "a.out"
inputs = [arg for arg in args if arg.endswith((".c", ".o", ".bc", ".a"))]
if not inputs:
    raise SystemExit(1)
# Optional external libraries must actually exist as bitcode archives.
for arg in args:
    if arg.startswith("-l") and arg not in ("-lc", "-lm"):
        raise SystemExit(1)
flags = []
skip = False
for arg in args:
    if skip:
        skip = False
        continue
    if arg == "-o":
        skip = True
        continue
    if (
        arg in inputs
        or arg.startswith(("-l", "-L", "-Wl,"))
        or arg in ("-static", "-s")
    ):
        continue
    flags.append(arg)
with tempfile.TemporaryDirectory(prefix="extreme-configure-") as temporary:
    directory = Path(temporary)
    objects = []
    for index, source in enumerate(inputs):
        if source.endswith(".c"):
            obj = directory / f"{index}.bc"
            subprocess.run(
                [str(root / "cc.py"), *flags, "-c", source, "-o", str(obj)], check=True
            )
            objects.append(str(obj))
        else:
            objects.append(source)
    runtime = base / "libc/platform.bc"
    if runtime.exists():
        objects.append(str(runtime))
    main = directory / "main.bc"
    subprocess.run([str(llvm / "llvm-link"), *objects, "-o", str(main)], check=True)
    linked = directory / "linked.bc"
    subprocess.run(
        [
            str(llvm / "llvm-link"),
            "--only-needed",
            str(main),
            str(base / "libc/lib/libc.a"),
            "-o",
            str(linked),
        ],
        check=True,
    )
    subprocess.run(
        [
            str(llvm / "opt"),
            "-passes=internalize,globaldce,default<O2>,globaldce",
            "--internalize-public-api-list=main",
            str(linked),
            "-o",
            output,
        ],
        check=True,
    )
    undefined = subprocess.check_output(
        [str(llvm / "llvm-nm"), "--undefined-only", output], text=True
    )
    native = {
        "__extreme_syscall",
        "__extreme_thread_pointer",
        "__extreme_activation_size",
        "__extreme_context_enter",
        "__extreme_context_save",
        "__extreme_context_restore",
        "setjmp",
        "longjmp",
        "_setjmp",
        "_longjmp",
        "sigsetjmp",
        "siglongjmp",
    }
    for line in undefined.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[-2] == "U" and fields[-1] not in native:
            print("unresolved Extreme interface: " + fields[-1], file=sys.stderr)
            raise SystemExit(1)
    Path(output).chmod(0o755)
