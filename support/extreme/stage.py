#!/usr/bin/env python3
"""Cache an external package stage by its checked-in build inputs."""

import argparse
import hashlib
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--stamp", type=Path, required=True)
p.add_argument("--artifact", type=Path, required=True)
p.add_argument("--input", type=Path, action="append", default=[])
p.add_argument("command", nargs=argparse.REMAINDER)
a = p.parse_args()
digest = hashlib.sha256()
for path in sorted(a.input):
    paths = (
        sorted(p for p in path.rglob("*") if p.is_file()) if path.is_dir() else [path]
    )
    for entry in paths:
        digest.update(str(entry).encode())
        digest.update(entry.read_bytes())
command = a.command[1:] if a.command[:1] == ["--"] else a.command
digest.update(repr(command).encode())
value = digest.hexdigest()
if a.artifact.exists() and a.stamp.exists() and a.stamp.read_text() == value:
    print(f"Up to date: {a.artifact}")
else:
    subprocess.run(command, check=True)
    if not a.artifact.exists():
        raise SystemExit("stage did not produce its artifact")
    a.stamp.parent.mkdir(parents=True, exist_ok=True)
    a.stamp.write_text(value)
