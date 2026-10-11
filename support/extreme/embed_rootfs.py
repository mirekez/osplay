#!/usr/bin/env python3
"""Initialize the native board's RAM block device in an Extreme image."""

import argparse
from pathlib import Path
import struct

p = argparse.ArgumentParser()
p.add_argument("image", type=Path)
p.add_argument("rootfs", type=Path)
a = p.parse_args()
rows = Path(str(a.image) + ".map").read_text().splitlines()
addresses = [
    int(row.split()[2])
    for row in rows
    if row.startswith("global ")
    and row.split()[1] == "_ZN6osplay16extreme_platform9root_diskE"
]
if len(addresses) != 1:
    raise SystemExit("native root-disk symbol missing or ambiguous")
rootfs = a.rootfs.read_bytes()
if len(rootfs) != 4 * 1024 * 1024:
    raise SystemExit("native root disk must be exactly 4 MiB")
with a.image.open("r+b") as image:
    header = struct.unpack("<10I", image.read(40))
    if header[:2] != (0x58434345, 1) or addresses[0] + len(rootfs) > header[9]:
        raise SystemExit("invalid Extreme image or root-disk range")
    image.seek(40 + addresses[0])
    image.write(rootfs)
