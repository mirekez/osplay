#!/usr/bin/env python3
"""DPI bridge framing and backpressure regression, without a CPU build."""
import ctypes as c
import os
from pathlib import Path
import socket
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
chipyard = (Path(__file__).resolve().parent / "chipyard").resolve(strict=True)
env = Path(os.environ.get("CHIPYARD_ENV", chipyard / ".conda-env"))
with tempfile.TemporaryDirectory(prefix="rocket-bridge-test-") as tmp:
    lib = Path(tmp) / "bridge.so"
    subprocess.run([os.environ.get("HOST_CXX", "g++"), "-std=c++17", "-shared", "-fPIC",
                    "-Wall", "-Wextra", "-Werror", "-I" + str(env / "share/verilator/include/vltstd"),
                    str(ROOT / "tests/rocket/net_bridge.cpp"), "-o", str(lib)], check=True)
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as peer:
        peer.bind(tmp + "/net.sock")
        peer.settimeout(1)
        os.environ["ROCKET_ETH_SOCKET"] = tmp + "/net.sock"
        tick = c.CDLL(str(lib)).mikos_net_tick
        tick.argtypes = [c.c_uint8, c.c_uint8, c.c_uint64, c.c_uint8, c.c_uint8, c.c_uint8,
                         c.POINTER(c.c_uint8), c.POINTER(c.c_uint64), c.POINTER(c.c_uint8), c.POINTER(c.c_uint8)]
        def step(reset=0, tx=0, data=0, keep=0, last=0, rx=0):
            v, d, k, l = c.c_uint8(), c.c_uint64(), c.c_uint8(), c.c_uint8()
            tick(reset, tx, data, keep, last, rx, c.byref(v), c.byref(d), c.byref(k), c.byref(l))
            return v.value, d.value, k.value, l.value
        step(reset=1)
        step()
        hello, remote = peer.recvfrom(2048)
        assert hello == b"\1"
        for length in (42, 60, 61, 64, 65, 1514):
            frame = bytes(i & 255 for i in range(length))
            peer.sendto(b"\2" + length.to_bytes(2, "big") + frame, remote)
            for _ in range(512):
                beat = step()
                if beat[0]: break
            assert beat[0]
            received = bytearray()
            while beat[0]:
                for _ in range(17): assert step() == beat, "RX changed under backpressure"
                received.extend(beat[1].to_bytes(8, "little")[:beat[2].bit_count()])
                last = beat[3]
                beat = step(rx=1)
                if last: break
            assert received == frame.ljust(60, b"\0")
            for i in range(0, len(received), 8):
                chunk = received[i:i+8]
                step(tx=1, data=int.from_bytes(chunk, "little"), keep=(1 << len(chunk))-1,
                     last=i+8 >= len(received))
            packet, _ = peer.recvfrom(2048)
            assert packet == b"\2" + len(received).to_bytes(2, "big") + received
        step(reset=1)
print("PASS: Rocket DPI bridge framing, padding, partial beats, TX, RX stalls and reset")
