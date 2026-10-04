#!/usr/bin/env python3
"""Run real Rocket RTL and verify UART plus bidirectional Ethernet frames."""
import argparse
import os
from pathlib import Path
import selectors
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
HOST = bytes.fromhex("020000000001")
GUEST = bytes.fromhex("020000000002")
HIP = socket.inet_aton("192.168.76.1")
GIP = socket.inet_aton("192.168.76.2")


def checksum(data):
    data += b"\0" * (len(data) % 2)
    total = sum(struct.unpack("!" + "H" * (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def arp_request():
    return b"\xff" * 6 + HOST + b"\x08\x06" + struct.pack(
        "!HHBBH6s4s6s4s", 1, 0x800, 6, 4, 1, HOST, HIP, b"\0" * 6, GIP)


PAYLOAD = bytes((i * 37 + 11) & 255 for i in range(512))


def echo_request():
    icmp = struct.pack("!BBHHH", 8, 0, 0, 7, 9) + PAYLOAD
    icmp = icmp[:2] + struct.pack("!H", checksum(icmp)) + icmp[4:]
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(icmp), 0x1234, 0,
                     32, 1, 0, HIP, GIP)
    ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
    return GUEST + HOST + b"\x08\x00" + ip + icmp


def arp_ok(frame):
    return (len(frame) >= 42 and frame[:12] == HOST + GUEST
            and frame[12:22] == bytes.fromhex("08060001080006040002")
            and frame[22:42] == GUEST + GIP + HOST + HIP)


def echo_ok(frame):
    return (len(frame) >= 554 and frame[:14] == HOST + GUEST + b"\x08\x00"
            and frame[14] == 0x45 and frame[23] == 1
            and frame[26:34] == GIP + HIP and checksum(frame[14:34]) == 0
            and frame[34:36] == b"\0\0" and frame[38:42] == b"\0\7\0\11"
            and frame[42:554] == PAYLOAD and checksum(frame[34:554]) == 0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("boot", "ping", "process"))
    args = parser.parse_args()
    out = ROOT / "build/tests/rocket"
    out.mkdir(parents=True, exist_ok=True)
    log_path = out / (args.mode + ".log")
    started = time.monotonic()
    default_timeout = "7200" if args.mode == "process" else "3600"
    deadline = started + float(os.environ.get("ROCKET_TIMEOUT", default_timeout))
    with tempfile.TemporaryDirectory(prefix="mikos-rocket-") as runtime, \
            socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as peer, \
            log_path.open("wb", buffering=0) as log:
        path = str(Path(runtime) / "net.sock")
        peer.bind(path)
        peer.setblocking(False)
        env = dict(os.environ, ROCKET_ETH_SOCKET=path)
        proc = subprocess.Popen(["bash", str(ROOT / "tests/rocket/run_rocket.sh")],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, env=env)
        selector = selectors.DefaultSelector()
        selector.register(peer, selectors.EVENT_READ)
        selector.register(proc.stdout, selectors.EVENT_READ)
        text = ""
        remote = None
        arp = echo = sent_command = False
        next_packet = 0
        reported = set()
        try:
            while time.monotonic() < deadline:
                for key, _ in selector.select(0.1):
                    if key.fileobj is proc.stdout:
                        chunk = os.read(proc.stdout.fileno(), 65536)
                        if not chunk:
                            selector.unregister(proc.stdout)
                        log.write(chunk)
                        text += chunk.decode(errors="replace").replace("\r", "")
                    else:
                        message, sender = peer.recvfrom(4096)
                        if message == b"\1":
                            remote = sender
                        elif len(message) >= 3 and message[0] == 2:
                            length = int.from_bytes(message[1:3], "big")
                            if len(message) != length + 3:
                                raise RuntimeError("malformed Ethernet bridge packet")
                            frame = message[3:]
                            if arp_ok(frame) and not arp:
                                arp = True
                                next_packet = 0
                                print("PASS: real Rocket DMA ARP reply", flush=True)
                            if echo_ok(frame) and not echo:
                                echo = True
                                print("PASS: real Rocket DMA ICMP reply, 512 payload bytes and checksums", flush=True)
                for marker in ("MIKOS:BOOT", "MIKOS:KERNEL_CXX_OK", "MIKOS:EXT4_ROOT_OK",
                               "MIKOS:BUSYBOX_ENTRY", "MIKOS:DROPBEAR_ENTRY", "MIKOS:TCP_LISTEN 22",
                               "ROCKET_PROCESS_OK"):
                    if marker + "\n" in text and marker not in reported:
                        reported.add(marker)
                        print(f"[{time.monotonic() - started:.1f}s] {marker}", flush=True)
                if args.mode == "boot" and "MIKOS:FLAT_DEVICE_IRQ_OFF\n" in text:
                    if "MIKOS:KERNEL_CXX_OK\n" not in text:
                        raise RuntimeError("missing C++ runtime proof")
                    print(f"PASS: Rocket boot and C++ runtime; log: {log_path}")
                    return
                if (remote and args.mode == "ping" and not echo
                        and "MIKOS:NETWORK_TIMER_ON\n" in text
                        and time.monotonic() >= next_packet):
                    # Wall time is much faster than simulated time. Avoid filling
                    # the NIC with retries while the guest is still booting.
                    frame = echo_request() if arp else arp_request()
                    message = b"\2" + len(frame).to_bytes(2, "big") + frame
                    try:
                        peer.sendto(message, remote)
                    except (BlockingIOError, ConnectionRefusedError, FileNotFoundError):
                        pass
                    next_packet = time.monotonic() + 30
                ready = "/ # " in text and (args.mode == "process" or echo)
                if ready and not sent_command:
                    if args.mode == "process":
                        command = ("echo ROCKET_FILE_OK >/rocket-file; cat /rocket-file; "
                                   "mv /rocket-file /rocket-moved; cat /rocket-moved; "
                                   "rm /rocket-moved; test ! -e /rocket-moved && "
                                   "echo ROCKET_PROCESS_OK; exit\n")
                    else:
                        command = "echo ROCKET_SHELL_OK; exit\n"
                    proc.stdin.write(command.encode())
                    proc.stdin.flush()
                    sent_command = True
                if proc.poll() is not None:
                    remaining = proc.stdout.read()
                    log.write(remaining)
                    text += remaining.decode(errors="replace").replace("\r", "")
                    if proc.returncode:
                        raise RuntimeError(f"simulator exit {proc.returncode}")
                    for marker in ("MIKOS:BOOT", "MIKOS:KERNEL_CXX_OK", "MIKOS:EXT4_ROOT_OK",
                                   "MIKOS:PMP_ON", "MIKOS:BUSYBOX_EXIT 0", "MIKOS:EXIT 0"):
                        if marker + "\n" not in text:
                            raise RuntimeError(f"missing {marker}")
                    marker = "ROCKET_SHELL_OK" if args.mode == "ping" else "ROCKET_PROCESS_OK"
                    if "\n" + marker + "\n" not in text or not sent_command:
                        raise RuntimeError("UART shell command was not executed")
                    if args.mode == "process" and text.splitlines().count("ROCKET_FILE_OK") != 2:
                        raise RuntimeError("ext4 write/read/rename did not preserve file contents")
                    if args.mode == "ping" and not (arp and echo):
                        raise RuntimeError("network peer did not verify both replies")
                    print(f"PASS: Rocket {args.mode}, ext4, U-mode BusyBox and clean HTIF exit; log: {log_path}")
                    return
            raise RuntimeError(f"timeout, last output:\n{text[-3000:]}")
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            selector.close()
            Path(f"/tmp/mikos-rocket-net-{proc.pid}.sock").unlink(missing_ok=True)
            print(f"UART log: {log_path}", flush=True)


if __name__ == "__main__":
    main()
