#!/usr/bin/env python3
"""Native Extreme + mikOS application acceptance through UART and real Ethernet."""

import argparse
import errno
import fcntl
import os
import pty
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import termios
import sys
import tempfile
import time
from interactive_network import DEFAULT_SOCKET, InteractiveLink

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(os.environ.get("EXTREME_BUILD_ROOT", str(ROOT / "build/tests/extreme")))


def run(args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--test",
        choices=("boot", "descriptors", "process", "ping", "tcp", "ssh", "interactive"),
        default="interactive",
    )
    parser.add_argument("--verilator", action="store_true")
    parser.add_argument("--multicore", action="store_true")
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument(
        "--progress",
        action="store_true",
        help="show simulator progress every 10 million cycles during interactive use",
    )
    link_options = parser.add_mutually_exclusive_group()
    link_options.add_argument("--tap-socket")
    link_options.add_argument(
        "--no-network",
        action="store_true",
        help="interactive UART console without Ethernet",
    )
    args = parser.parse_args()
    if args.no_network and args.test != "interactive":
        parser.error("--no-network applies only to interactive sessions")
    network = args.test in ("ping", "tcp", "ssh")
    shared_link = args.test == "interactive" or (
        network
        and (
            args.tap_socket
            or os.environ.get("MIKOS_TAP")
            or os.environ.get("MIKOS_TAP_SOCKET")
        )
    )
    link = None
    if shared_link and not args.no_network:
        args.tap_socket = args.tap_socket or os.environ.get(
            "MIKOS_TAP_SOCKET", DEFAULT_SOCKET
        )
        try:
            link = InteractiveLink.from_environment(args.tap_socket)
            link.check()
        except (
            OSError,
            ValueError,
            RuntimeError,
            subprocess.CalledProcessError,
        ) as error:
            print(str(error), file=sys.stderr)
            if link:
                print(link.setup_hint(ROOT, sys.argv[1:]), file=sys.stderr)
            return 1
    if network and not args.tap_socket and os.environ.get("EXTREME_NETNS") != "1":
        os.execvp(
            "unshare",
            [
                "unshare",
                "-Urn",
                "env",
                "EXTREME_NETNS=1",
                sys.executable,
                __file__,
                *sys.argv[1:],
            ],
        )
    cores = 3 if args.multicore else 1
    flow = "verilator" if args.verilator else "cpp"
    if not args.no_build:
        run(
            [
                "bash",
                ROOT / "support/extreme/prepare.sh",
                "--cores",
                cores,
                *(["--verilator"] if args.verilator else []),
            ]
        )
    default_board = (
        BASE
        / f"userspace-c{cores}"
        / ("rtl/obj/board" if args.verilator else "cpu/devices/ExtremeBoard_cpp")
    )
    board = Path(
        os.environ.get(
            "EXTREME_BOARD_SV" if args.verilator else "EXTREME_BOARD_CPP",
            str(default_board),
        )
    )
    if args.test == "descriptors" and not args.no_build:
        run(
            ["bash", ROOT / "support/extreme/prepare_userspace.sh"],
            env={
                **os.environ,
                "EXTREME_COMPILER": str(
                    BASE / f"userspace-c{cores}/cpu/bin/extreme-cxx"
                ),
                "EXTREME_DROPBEAR": "0",
                "EXTREME_USER_BUILD": str(BASE / "userspace-descriptors"),
                "EXTREME_TEST_APPLICATION": str(
                    ROOT / "tests/extreme/descriptor_metadata.c"
                ),
            },
        )
    image_folder = (
        "userspace-descriptors" if args.test == "descriptors" else "userspace"
    )
    image = Path(
        os.environ.get("EXTREME_USER_IMAGE", str(BASE / image_folder / "mikos.ecx"))
    )
    bridge = Path(os.environ.get("EXTREME_BRIDGE", str(BASE / "host/ethgig_tap")))
    client = Path(os.environ.get("EXTREME_SSH_CLIENT", str(BASE / "host/dbclient")))
    timeout = int(os.environ.get("EXTREME_USER_TIMEOUT", "7200"))
    results = BASE / "results" / f"userspace-{args.test}-c{cores}-{flow}"
    results.mkdir(parents=True, exist_ok=True)
    profile = (
        "ssh"
        if args.test == "ssh" or (args.test == "interactive" and link is not None)
        else "process" if args.test == "process" else "interactive"
    )
    environment = dict(os.environ)
    if args.test not in ("boot", "descriptors"):
        environment["EXTREME_INIT_SCRIPT"] = str(
            ROOT / f"tests/extreme/rootfs/rcS.{profile}"
        )
    # Each run receives its own disk and program image; guest writes remain local.
    with tempfile.TemporaryDirectory(
        prefix="extreme-userspace-", dir=os.environ.get("EXTREME_SCRATCH", "/dev/shm")
    ) as temporary:
        if args.test == "interactive":
            print(
                "Preparing private mikOS root filesystem...",
                file=sys.stderr,
                flush=True,
            )
        directory = Path(temporary)
        environment["EXTREME_ROOTFS_STAGE"] = str(directory / "rootfs")
        environment["EXTREME_ROOTFS_IMAGE"] = str(directory / "rootfs.ext4")
        run(
            ["bash", ROOT / "support/extreme/prepare_rootfs.sh"],
            env=environment,
            stdout=subprocess.DEVNULL,
        )
        payload = directory / "mikos.ecx"
        shutil.copyfile(image, payload)
        shutil.copyfile(str(image) + ".map", str(payload) + ".map")
        run(
            [
                sys.executable,
                ROOT / "support/extreme/embed_rootfs.py",
                payload,
                directory / "rootfs.ext4",
            ]
        )
        children = []
        log_path = results / "uart.log"
        deadline = time.monotonic() + timeout
        simulator = None

        def wait_for(marker):
            while time.monotonic() < deadline:
                if marker in log_path.read_text(errors="replace"):
                    return
                if simulator.poll() is not None:
                    raise RuntimeError(f"CPU stopped before {marker}")
                time.sleep(0.1)
            raise TimeoutError(f"CPU did not reach {marker}")

        try:
            media = args.tap_socket
            if network and not media:
                media = str(directory / "ethernet.sock")
                bridge_log = (results / "bridge.log").open("w")
                bridge_process = subprocess.Popen(
                    [
                        str(bridge),
                        "--tap",
                        "extreme0",
                        "--address",
                        "192.168.76.1/24",
                        "--socket",
                        media,
                    ],
                    stdout=bridge_log,
                    stderr=subprocess.STDOUT,
                )
                children.append(bridge_process)
                for _ in range(200):
                    if Path(media).is_socket():
                        break
                    if bridge_process.poll() is not None:
                        raise RuntimeError("TAP bridge failed")
                    time.sleep(0.05)
                if not Path(media).is_socket():
                    raise RuntimeError("TAP bridge did not start")
                run(
                    ["sysctl", "-q", "-w", "net.ipv6.conf.extreme0.disable_ipv6=1"],
                    stdout=subprocess.DEVNULL,
                )
                run(
                    [
                        "ip",
                        "neigh",
                        "replace",
                        "192.168.76.2",
                        "lladdr",
                        "02:00:00:00:00:02",
                        "nud",
                        "permanent",
                        "dev",
                        "extreme0",
                    ]
                )
            command = [str(board), "--image", str(payload), "--cycles", "2000000000"]
            if args.test != "interactive" or args.progress:
                command += ["--progress", "10000000"]
            if media:
                command += ["--socket", media]
            if args.test == "interactive":
                command += ["--allow-idle-network"]
                if link:
                    print(
                        link.connection_hint(client, BASE / "test_key.dropbear"),
                        file=sys.stderr,
                        flush=True,
                    )
                print(
                    f"Starting Extreme {flow} simulation: loading RAM and booting mikOS.\n"
                    "Startup can take several minutes, especially with Verilator. "
                    "Use --progress for cycle updates; Ctrl-C stops the simulator.",
                    file=sys.stderr,
                    flush=True,
                )
                run(command, timeout=timeout)
                return
            with log_path.open("w") as log, (results / "model.log").open(
                "w"
            ) as model_log:
                simulator = subprocess.Popen(
                    command, stdin=subprocess.PIPE, stdout=log, stderr=model_log
                )
                children.append(simulator)

                def uart(text):
                    simulator.stdin.write(text.encode())
                    simulator.stdin.flush()

                def ssh_command(command, log, terminal):
                    master = slave = None
                    try:
                        if terminal:
                            master, slave = pty.openpty()
                            fcntl.ioctl(
                                slave,
                                termios.TIOCSWINSZ,
                                struct.pack("HHHH", 24, 80, 0, 0),
                            )
                            os.set_blocking(master, False)
                        process = subprocess.Popen(
                            list(map(str, command)),
                            stdin=slave if terminal else subprocess.DEVNULL,
                            stdout=slave if terminal else log,
                            stderr=slave if terminal else subprocess.STDOUT,
                            start_new_session=terminal,
                        )
                        children.append(process)
                        if slave is not None:
                            os.close(slave)
                            slave = None

                        def collect():
                            if master is None:
                                return
                            while True:
                                try:
                                    data = os.read(master, 65536)
                                except BlockingIOError:
                                    break
                                except OSError as error:
                                    if error.errno == errno.EIO:
                                        break
                                    raise
                                if not data:
                                    break
                                log.write(data.decode(errors="replace"))
                            log.flush()

                        while process.poll() is None:
                            collect()
                            if simulator.poll() is not None:
                                raise RuntimeError("CPU stopped during the SSH session")
                            if time.monotonic() >= deadline:
                                raise TimeoutError("SSH acceptance deadline expired")
                            time.sleep(0.1)
                        collect()
                        if process.returncode:
                            raise RuntimeError(
                                f"SSH client failed: {process.returncode}"
                            )
                    finally:
                        for descriptor in (master, slave):
                            if descriptor is not None:
                                os.close(descriptor)

                if args.test in ("boot", "descriptors", "process"):
                    code = simulator.wait(timeout=timeout)
                    if code:
                        raise RuntimeError(f"CPU regression failed: {code}")
                    markers = (
                        ["MIKOS:BUSYBOX_OK"]
                        if args.test == "boot"
                        else (
                            ["MIKOS_DESCRIPTOR_METADATA_OK"]
                            if args.test == "descriptors"
                            else [
                                "MIKOS_FD10_CHILD",
                                "MIKOS_FD10_PARENT",
                                "MIKOS_THREE_STAGE",
                                "MIKOS_ADDR_parent",
                                "MIKOS_WAIT_7",
                                "MIKOS_NESTED_LS_0",
                                "MIKOS_FLOAT64_OK",
                                "MIKOS_SIGNAL_OK",
                                "MIKOS_PROCESS_OK",
                            ]
                        )
                    )
                    for marker in markers:
                        wait_for(marker)
                else:
                    wait_for("/ # ")
                    with (results / "ping.log").open("w") as ping_log:
                        run(
                            ["ping", "-n", "-c", "2", "-W", "30", "192.168.76.2"],
                            stdout=ping_log,
                            stderr=subprocess.STDOUT,
                            timeout=90,
                        )
                    if args.test == "tcp":
                        uart("nc -l -p 2223 -e /bin/sh\n")
                        wait_for("MIKOS:TCP_LISTEN 2223")
                        with socket.create_connection(
                            ("192.168.76.2", 2223), timeout=120
                        ) as peer:
                            peer.settimeout(timeout)
                            peer.sendall(
                                b"echo MIKOS_TCP_STREAM_OK >/tcp-received\nexit\n"
                            )
                            peer.shutdown(socket.SHUT_WR)
                            while peer.recv(4096):
                                pass
                        uart("cat /tcp-received\n")
                        wait_for("MIKOS_TCP_STREAM_OK")
                    if args.test == "ssh":
                        wait_for("MIKOS:TCP_LISTEN 2222")
                        for session in (1, 2, 3):
                            marker = f"MIKOS_SSH_AUTH_OK_{session}"
                            terminal = session == 1
                            remote = (
                                f"tty && sh -c 'ls /' && echo {marker}; exit 0"
                                if terminal
                                else f"echo {marker}; exit 0"
                            )
                            with (results / f"ssh-{session}.log").open("w") as ssh_log:
                                ssh_command(
                                    [
                                        client,
                                        "-i",
                                        BASE / "test_key.dropbear",
                                        "-c",
                                        "none",
                                        "-y",
                                        "-y",
                                        "-t" if terminal else "-T",
                                        "-p",
                                        "2222",
                                        "root@192.168.76.2",
                                        remote,
                                    ],
                                    ssh_log,
                                    terminal,
                                )
                            if (
                                marker
                                not in (results / f"ssh-{session}.log").read_text()
                            ):
                                raise RuntimeError("SSH command output missing")
                            if (
                                terminal
                                and "/dev/pts/"
                                not in (results / f"ssh-{session}.log").read_text()
                            ):
                                raise RuntimeError(
                                    "SSH pseudo-terminal was not allocated"
                                )
                    uart("exit 0\n")
                    if simulator.wait(timeout=timeout):
                        raise RuntimeError("CPU shutdown failed")
                wait_for("Extreme board PASS")
            print(
                f"PASS: Extreme {args.test}, {cores} core(s), {flow}; logs: {results}"
            )
        except Exception:
            for path in (log_path, results / "model.log"):
                if path.exists():
                    print(path.read_text(errors="replace")[-16000:], file=sys.stderr)
            raise
        finally:
            for process in reversed(children):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()


if __name__ == "__main__":
    sys.exit(main())
