#!/usr/bin/env python3
"""Rootless proxy framing plus libslirp ARP/ICMP and localhost binding."""
import importlib.util
import selectors
import socket
import struct
import subprocess
import tempfile
import time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('rocket_peer', root/'tests/rocket/test_sim.py')
peer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(peer)

with tempfile.TemporaryDirectory(prefix='osplay-proxy-test-') as temporary:
    path = Path(temporary)
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reserve:
        reserve.bind(('127.0.0.1', 0))
        port = reserve.getsockname()[1]
    log = (path/'proxy.log').open('w')
    process = subprocess.Popen([str(root/'build/tests/network/ethgig_proxy'),
        '--socket', str(path/'proxy.sock'), '--ssh-port', str(port), '--clock-scale', '1'],
        stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic()+10
        while not (path/'proxy.sock').exists():
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError((path/'proxy.log').read_text())
            time.sleep(0.01)
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as guest:
            guest.bind(str(path/'guest.sock'))
            guest.settimeout(5)
            guest.sendto(b'\1', str(path/'proxy.sock'))
            # Ask for slirp's virtual-host MAC, from the guest's real identity.
            request = b'\xff'*6+peer.GUEST+b'\x08\x06'+struct.pack(
                '!HHBBH6s4s6s4s', 1, 0x800, 6, 4, 1, peer.GUEST, peer.GIP, b'\0'*6, peer.HIP)
            guest.sendto(b'\2'+len(request).to_bytes(2, 'big')+request, str(path/'proxy.sock'))
            packet = guest.recv(2048)
            assert packet[0] == 2 and int.from_bytes(packet[1:3], 'big') == len(packet)-3
            frame = packet[3:]
            assert frame[:6] == peer.GUEST and frame[12:14] == b'\x08\x06'
            assert frame[20:22] == b'\0\2' and frame[28:32] == peer.HIP
            host_mac = frame[6:12]
            payload = b'osplay-rootless-proxy'
            icmp = struct.pack('!BBHHH', 8, 0, 0, 123, 1)+payload
            icmp = icmp[:2]+struct.pack('!H', peer.checksum(icmp))+icmp[4:]
            ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20+len(icmp), 1, 0, 64, 1, 0, peer.GIP, peer.HIP)
            ip = ip[:10]+struct.pack('!H', peer.checksum(ip))+ip[12:]
            request = host_mac+peer.GUEST+b'\x08\x00'+ip+icmp
            guest.sendto(b'\2'+len(request).to_bytes(2, 'big')+request, str(path/'proxy.sock'))
            frame = guest.recv(2048)[3:]
            assert frame[:6] == peer.GUEST and frame[23] == 1
            length = int.from_bytes(frame[16:18], 'big')
            assert frame[34:36] == b'\0\0' and frame[42:14+length] == payload
            assert peer.checksum(frame[14:34]) == 0 and peer.checksum(frame[34:14+length]) == 0
        print('Rootless Ethernet proxy: ARP/ICMP, checksums and framing PASSED')
    finally:
        process.terminate()
        process.wait(timeout=5)
        log.close()
