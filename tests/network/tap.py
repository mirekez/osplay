#!/usr/bin/env python3
"""Read-only checks for the shared Tribe/Extreme/Rocket Ethernet bridge."""
import argparse
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import sys

DEFAULT_TAP = "tap-osplay"
DEFAULT_SOCKET = "/tmp/osplay-tap.sock"
MAC = "02:00:00:00:00:02"
ROOT = Path(__file__).resolve().parents[2]


def ip_json(*args):
    return json.loads(subprocess.check_output(["ip", "-j", *args], text=True))


def select_tap(requested=None, host="192.168.76.1"):
    if requested:
        return requested
    owners = [link for link in ip_json("-d", "address", "show")
              if any(a.get("local") == host for a in link["addr_info"])]
    if not owners:
        return DEFAULT_TAP
    if len(owners) != 1:
        raise RuntimeError(f"Multiple interfaces own {host}; select OSPLAY_TAP explicitly")
    info = owners[0].get("linkinfo", {})
    if info.get("info_kind") != "tun" or info.get("info_data", {}).get("type") != "tap":
        raise RuntimeError(f"Host IP {host} belongs to non-TAP interface {owners[0]['ifname']}")
    return owners[0]["ifname"]


def check_link(tap, media, host, guest, mac=MAC):
    if not Path(media).is_socket():
        raise RuntimeError(f"TAP bridge socket is missing: {media}")
    # Do not send HELLO: a preflight check must not steal the active peer.
    with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as probe:
        probe.connect(media)
    devices = ip_json("-d", "link", "show", "dev", tap)
    info = devices[0].get("linkinfo", {}) if devices else {}
    if info.get("info_kind") != "tun" or info.get("info_data", {}).get("type") != "tap":
        raise RuntimeError(f"Not a TAP interface: {tap}")
    links = ip_json("address", "show", "dev", tap)
    if not links or "UP" not in links[0]["flags"]:
        raise RuntimeError(f"Host TAP interface is not up: {tap}")
    if not any(a.get("local") == host for a in links[0]["addr_info"]):
        raise RuntimeError(f"Host TAP {tap} does not own {host}")
    for link in ip_json("address", "show"):
        if any(a.get("local") == guest for a in link["addr_info"]):
            raise RuntimeError(
                f"Guest IP {guest} is already a local host address on {link['ifname']}. "
                "Use an isolated network namespace for this guest; do not overwrite host routes.")
    routes = ip_json("route", "get", guest, "from", host)
    if not routes or routes[0].get("dev") != tap:
        raise RuntimeError(f"Route to guest {guest} does not use {tap}")
    neighbors = ip_json("neigh", "show", "to", guest, "dev", tap)
    if not any(n.get("lladdr", "").lower() == mac.lower()
               and "PERMANENT" in n.get("state", []) for n in neighbors):
        command = ["sudo", "ip", "neigh", "replace", guest, "lladdr", mac,
                   "nud", "permanent", "dev", tap]
        raise RuntimeError("Permanent guest neighbor is missing. Run:\n  " + shlex.join(command))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", nargs="?", choices=("rocket", "tribe", "extreme"))
    parser.add_argument("--select-tap", action="store_true", help="print the selected interface without modifying it")
    parser.add_argument("--tap", default=os.environ.get("OSPLAY_TAP"))
    parser.add_argument("--socket", default=os.environ.get("OSPLAY_TAP_SOCKET", DEFAULT_SOCKET))
    parser.add_argument("--host")
    parser.add_argument("--guest")
    parser.add_argument("--mac", default=MAC)
    args = parser.parse_args()
    if not args.select_tap and not args.profile:
        parser.error("a profile is required unless --select-tap is used")
    host = args.host or "192.168.76.1"
    guest = args.guest or "192.168.76.2"
    try:
        args.tap = select_tap(args.tap, host)
        if args.select_tap:
            print(args.tap)
            return 0
        check_link(args.tap, args.socket, host, guest, args.mac)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Ethernet preflight failed: {error}", file=sys.stderr)
        if not args.tap:
            return 1
        print("Build the shared bridge as your normal user:", file=sys.stderr)
        print("  " + shlex.join(["bash", str(ROOT / "tests/network/build_tap.sh")]), file=sys.stderr)
        print("Create or repair the shared link (keeps an existing bridge running):", file=sys.stderr)
        print("  " + shlex.join(["sudo", "env", f"OSPLAY_TAP={args.tap}",
              f"OSPLAY_TAP_SOCKET={args.socket}", "bash",
              str(ROOT / "tests/network/start_tap.sh"), "--background"]), file=sys.stderr)
        return 1
    print(f"PASS: {args.profile} TAP {args.tap}, {host} -> {guest}, socket {args.socket}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
