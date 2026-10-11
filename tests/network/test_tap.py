#!/usr/bin/env python3
"""Run under unshare -Urn: real TAP checks and sequential framed Ethernet peers."""
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

from tap import check_link, select_tap, ROOT


def checksum(data):
    data += b"\0" * (len(data) % 2)
    value = sum(struct.unpack("!" + "H" * (len(data) // 2), data))
    while value >> 16:
        value = (value & 65535) + (value >> 16)
    return (~value) & 65535


def ping_peer(directory, media, tap, host, guest, index):
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as peer:
        peer.bind(str(directory / f"peer-{index}"))
        peer.connect(media)
        peer.settimeout(5)
        peer.send(b"\1")
        # Checks must not send HELLO or redirect packets away from this peer.
        check_link(tap, media, host, guest)
        ping = subprocess.Popen(["ping", "-n", "-I", host, "-c", "1", "-W", "5", guest],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                message = peer.recv(4096)
                assert message[0] == 2 and len(message) == 3 + int.from_bytes(message[1:3], "big")
                frame = message[3:]
                if frame[12:14] != b"\x08\x00" or frame[23] != 1:
                    continue
                size = (frame[14] & 15) * 4
                ip = bytearray(frame[14:14+size])
                icmp = bytearray(frame[14+size:14+int.from_bytes(ip[2:4], "big")])
                if icmp[0] != 8:
                    continue
                assert ip[16:20] == socket.inet_aton(guest)
                ip[12:16], ip[16:20] = ip[16:20], ip[12:16]
                ip[10:12] = b"\0\0"
                ip[10:12] = checksum(ip).to_bytes(2, "big")
                icmp[0] = 0
                icmp[2:4] = b"\0\0"
                icmp[2:4] = checksum(icmp).to_bytes(2, "big")
                reply = frame[6:12] + frame[:6] + b"\x08\x00" + ip + icmp
                peer.send(b"\2" + len(reply).to_bytes(2, "big") + reply)
                output, _ = ping.communicate(timeout=6)
                assert ping.returncode == 0, output.decode()
                return
            raise AssertionError("no ICMP request received")
        finally:
            if ping.poll() is None:
                ping.kill()
                ping.wait()


def main():
    with tempfile.TemporaryDirectory(prefix="osplay-tap-test-") as tmp:
        directory = Path(tmp)
        media = str(directory / "shared socket")
        tap = "tap-old-test"
        # Reproduce a stopped legacy bridge and an empty TAP left by failed setup.
        for name in (tap, "tap-osplay"):
            subprocess.run(["ip", "tuntap", "add", "dev", name, "mode", "tap"], check=True)
        subprocess.run(["ip", "address", "add", "192.168.76.1/24", "dev", tap], check=True)
        subprocess.run(["ip", "link", "set", "dev", tap, "up"], check=True)
        assert select_tap() == tap
        env = dict(os.environ, OSPLAY_TAP_SOCKET=media)
        env.pop("OSPLAY_TAP", None)
        with (directory / "bridge.log").open("w+") as log:
            bridge = subprocess.Popen(["bash", str(ROOT / "tests/network/start_tap.sh")],
                                      env=env, stdout=log, stderr=subprocess.STDOUT,
                                      start_new_session=True)
            try:
                for _ in range(100):
                    if Path(media).is_socket():
                        break
                    assert bridge.poll() is None, "bridge failed"
                    time.sleep(0.05)
                for index, (profile, host, guest) in enumerate([
                    ("rocket", "192.168.76.1", "192.168.76.2"),
                    ("extreme", "192.168.76.1", "192.168.76.2"),
                    ("tribe", "192.168.76.1", "192.168.76.2")]):
                    subprocess.run([sys.executable, str(ROOT / "tests/network/tap.py"), profile],
                                   env=env, check=True)
                    ping_peer(directory, media, tap, host, guest, index)
                    print(f"PASS: {profile} profile, real TAP ping and peer handover", flush=True)
                links = json.loads(subprocess.check_output(
                    ["ip", "-j", "-4", "address", "show", "dev", tap], text=True))
                assert [(a["local"], a["prefixlen"]) for a in links[0]["addr_info"]] == [
                    ("192.168.76.1", 24)]
                competing = subprocess.run(
                    ["bash", str(ROOT / "tests/network/start_tap.sh"), "--background"],
                    env=dict(env, OSPLAY_TAP_SOCKET=str(directory / "other socket")),
                    capture_output=True, text=True)
                assert competing.returncode != 0 and "already has an attached process" in competing.stderr
                assert not (directory / "other socket").exists() and bridge.poll() is None
                empty = json.loads(subprocess.check_output(
                    ["ip", "-j", "-4", "address", "show", "dev", "tap-osplay"], text=True))
                assert not any(link["addr_info"] for link in empty)
                print("PASS: auto-reuse idle legacy TAP; reject a competing bridge", flush=True)
                subprocess.run(["ip", "neigh", "del", "192.168.76.2", "dev", tap], check=True)
                try:
                    check_link(tap, media, "192.168.76.1", "192.168.76.2")
                    raise AssertionError("missing neighbor accepted")
                except RuntimeError as error:
                    assert "Permanent guest neighbor" in str(error)
                # Repair in place, without spawning a second bridge.
                inode = Path(media).stat().st_ino
                subprocess.run(["bash", str(ROOT / "tests/network/start_tap.sh"), "--background"],
                               env=env, check=True)
                assert Path(media).stat().st_ino == inode and bridge.poll() is None
                check_link(tap, media, "192.168.76.1", "192.168.76.2")
                subprocess.run(["ip", "address", "add", "192.168.76.2/32", "dev", "lo"], check=True)
                try:
                    check_link(tap, media, "192.168.76.1", "192.168.76.2")
                    raise AssertionError("local guest address conflict accepted")
                except RuntimeError as error:
                    assert "already a local host address" in str(error)
                with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as stale:
                    stale.bind(str(directory / "stale"))
                try:
                    check_link(tap, str(directory / "stale"), "192.168.76.1", "192.168.76.2")
                    raise AssertionError("stale socket accepted")
                except ConnectionRefusedError:
                    pass
                print("PASS: missing neighbor, in-place repair, local IP conflict and stale socket", flush=True)
            finally:
                os.killpg(bridge.pid, signal.SIGTERM) if bridge.poll() is None else None
                bridge.wait(timeout=10)
                log.seek(0)
                print(log.read(), end="")


if __name__ == "__main__":
    main()
