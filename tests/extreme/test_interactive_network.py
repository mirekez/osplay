#!/usr/bin/env python3
"""Run with unshare -Urn; checks use an isolated TAP and the real bridge helper."""

import argparse
import os
from pathlib import Path
import shlex
import signal
import socket
import subprocess
import sys
import tempfile
import time

from interactive_network import GUEST, MAC, InteractiveLink

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--console", action="store_true")
    parser.add_argument("--verilator", action="store_true")
    parser.add_argument(
        "--idle", action="store_true", help="exit without host network traffic"
    )
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="extreme-link-", dir="/tmp") as folder:
        directory = Path(folder)
        # Spaces exercise the shell quoting in the copyable setup commands.
        media = str(directory / "ethernet socket")
        os.environ.update(
            MIKOS_TAP="extreme-check",
            MIKOS_TAP_SOCKET=media,
            # Stale platform settings must not divert the universal link.
            EXTREME_INTERACTIVE_TAP="unused-old-tap",
            EXTREME_ETH_TAP_SOCKET="/tmp/unused-old-extreme.sock",
            EXTREME_INTERACTIVE_HOST_ADDRESS="192.168.76.3",
            EXTREME_BRIDGE="/missing/old-extreme-bridge",
        )
        link = InteractiveLink.from_environment(media)
        children = []
        with (directory / "bridge.log").open("w+") as bridge_log:
            try:
                bridge = subprocess.Popen(
                    ["bash", ROOT / "tests/extreme/start_tap.sh"],
                    stdout=bridge_log,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
                children.append(bridge)
                for _ in range(200):
                    bridge_log.seek(0)
                    if "Shared TAP ready" in bridge_log.read():
                        break
                    if bridge.poll() is not None:
                        raise RuntimeError("bridge setup failed")
                    time.sleep(0.05)
                link.check()
                for profile in ("extreme", "rocket", "tribe"):
                    subprocess.run(
                        [sys.executable, ROOT / "tests/network/tap.py", profile],
                        check=True,
                    )
                inode = Path(media).stat().st_ino
                subprocess.run(
                    ["bash", ROOT / "tests/extreme/start_tap.sh", "--background"],
                    check=True,
                )
                assert Path(media).stat().st_ino == inode and bridge.poll() is None
                subprocess.run(
                    ["ip", "neigh", "del", GUEST, "dev", link.tap], check=True
                )
                try:
                    link.check()
                    raise AssertionError("missing neighbor accepted")
                except RuntimeError as error:
                    assert "Permanent guest neighbor" in str(error)
                subprocess.run(
                    [
                        "ip",
                        "neigh",
                        "replace",
                        GUEST,
                        "lladdr",
                        MAC,
                        "nud",
                        "permanent",
                        "dev",
                        link.tap,
                    ],
                    check=True,
                )
                try:
                    InteractiveLink(link.tap, media, "192.168.76.3").check()
                    raise AssertionError("wrong host address accepted")
                except RuntimeError as error:
                    assert "does not own" in str(error)
                with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as stale:
                    stale.bind(str(directory / "stale"))
                try:
                    InteractiveLink(
                        link.tap, str(directory / "stale"), link.host
                    ).check()
                    raise AssertionError("stale socket accepted")
                except ConnectionRefusedError:
                    pass
                missing = subprocess.run(
                    [
                        sys.executable,
                        ROOT / "tests/extreme/userspace.py",
                        "--no-build",
                        "--tap-socket",
                        str(directory / "missing socket"),
                    ],
                    capture_output=True,
                    text=True,
                )
                assert missing.returncode == 1 and "socket is missing" in missing.stderr
                setup = next(
                    line.strip()
                    for line in missing.stderr.splitlines()
                    if line.strip().startswith("sudo env ")
                )
                assert (
                    f"MIKOS_TAP_SOCKET={directory / 'missing socket'}"
                    in shlex.split(setup)
                )
                assert "MIKOS_TAP=extreme-check" in shlex.split(setup)
                assert str(ROOT / "tests/network/start_tap.sh") in shlex.split(setup)
                assert "EXTREME_ETH_TAP_SOCKET" not in missing.stderr
                assert "tap.py extreme" in missing.stderr
                shared_missing = dict(
                    os.environ, MIKOS_TAP_SOCKET=str(directory / "shared missing")
                )
                for profile in ("ping", "tcp", "ssh"):
                    result = subprocess.run(
                        [
                            sys.executable,
                            ROOT / "tests/extreme/userspace.py",
                            "--test",
                            profile,
                            "--no-build",
                        ],
                        env=shared_missing,
                        capture_output=True,
                        text=True,
                    )
                    assert result.returncode == 1 and "shared missing" in result.stderr
                assert "--no-network" in missing.stderr
                assert "Preparing private" not in missing.stderr
                print(
                    "PASS: real TAP address, neighbor checks, stale socket and quoted setup hints",
                    flush=True,
                )
                if not args.console:
                    return
                results = (
                    ROOT
                    / "build/tests/extreme/results"
                    / ("interactive-link-" + ("verilator" if args.verilator else "cpp"))
                )
                results.mkdir(parents=True, exist_ok=True)
                log_path = results / "console.log"
                with log_path.open("w") as log:
                    console = subprocess.Popen(
                        [
                            sys.executable,
                            ROOT / "tests/extreme/userspace.py",
                            "--no-build",
                            *(["--verilator"] if args.verilator else []),
                        ],
                        stdin=subprocess.PIPE,
                        stdout=log,
                        stderr=subprocess.STDOUT,
                        start_new_session=True,
                    )
                    children.append(console)
                    deadline = time.monotonic() + 1800
                    while True:
                        output = log_path.read_text(errors="replace")
                        if "/ # " in output and "MIKOS:TCP_LISTEN 2222" in output:
                            break
                        if console.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError(
                                f"interactive boot failed; see {log_path}"
                            )
                        time.sleep(0.2)
                    assert "-p 2222 root@192.168.76.2" in output
                    assert f"MIKOS:NET_IP {GUEST}" in output
                    if not args.idle:
                        ping = next(
                            line.strip()
                            for line in output.splitlines()
                            if line.strip().startswith("ping -I ")
                        )
                        subprocess.run(shlex.split(ping), check=True, timeout=90)
                    console.stdin.write(b"echo EXTREME_INTERACTIVE_LINK_OK\nexit 0\n")
                    console.stdin.flush()
                    assert console.wait(timeout=300) == 0, log_path
                    output = log_path.read_text()
                    assert (
                        "EXTREME_INTERACTIVE_LINK_OK" in output
                        and "Extreme board PASS" in output
                    )
                    print(
                        f"PASS: interactive console, SSH listener and clean shutdown; {log_path}"
                    )
            finally:
                for child in reversed(children):
                    try:
                        os.killpg(child.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    if child.poll() is None:
                        try:
                            child.wait(timeout=10)
                        except subprocess.TimeoutExpired:
                            child.kill()
                            child.wait()
                bridge_log.seek(0)
                print(bridge_log.read(), end="")


if __name__ == "__main__":
    main()
