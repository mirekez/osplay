#!/usr/bin/env python3
"""Optional host TCP/SSH acceptance against the real Rocket TAP connection."""
import argparse
import os
from pathlib import Path
import shlex
import socket
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("tcp", "ssh"))
    args = parser.parse_args()
    path = os.environ.get("ROCKET_ETH_TAP_SOCKET", os.environ.get("ROCKET_ETH_SOCKET", os.environ.get("MIKOS_TAP_SOCKET", "/tmp/mikos-tap.sock")))
    if not Path(path).is_socket():
        raise SystemExit("Start tests/rocket/start_tap.sh first; missing " + path)
    subprocess.run(["python3", str(ROOT / "tests/network/tap.py"), "rocket",
                    "--tap", os.environ.get("ROCKET_INTERACTIVE_TAP", os.environ.get("MIKOS_TAP", "")),
                    "--socket", path], check=True)
    timeout = float(os.environ.get("ROCKET_TIMEOUT", "3600"))
    log_path = ROOT / "build/tests/rocket" / (args.mode + ".log")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    text = []
    with log_path.open("wb", buffering=0) as log:
        proc = subprocess.Popen(["bash", str(ROOT / "tests/rocket/run_rocket.sh")],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, env=dict(os.environ, ROCKET_ETH_SOCKET=path))
        def read():
            reported = set()
            while chunk := os.read(proc.stdout.fileno(), 4096):
                log.write(chunk)
                text.append(chunk.decode(errors="replace"))
                output = "".join(text).replace("\r", "")
                for marker in ("MIKOS:BOOT", "MIKOS:KERNEL_CXX_OK", "MIKOS:EXT4_ROOT_OK",
                               "MIKOS:BUSYBOX_ENTRY", "MIKOS:DROPBEAR_ENTRY", "MIKOS:TCP_LISTEN 22",
                               "ROCKET_PROCESS_OK", "MIKOS:TCP_LISTEN 2222"):
                    if "\n" + marker + "\n" in "\n" + output and marker not in reported:
                        reported.add(marker)
                        print(f"[{time.monotonic() - started:.1f}s] {marker}", flush=True)
        reader = threading.Thread(target=read, daemon=True)
        reader.start()
        def wait(marker, start=0):
            deadline = time.monotonic() + timeout
            while marker not in "".join(text)[start:]:
                if proc.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError(f"waiting for {marker}; see {log_path}")
                time.sleep(0.1)
        def uart(command):
            proc.stdin.write(command.encode() + b"\n")
            proc.stdin.flush()
        try:
            wait("/ # ")
            if args.mode == "tcp":
                uart("echo ROCKET_FILE_OK >/rocket-file; cat /rocket-file; "
                     "mv /rocket-file /rocket-moved; cat /rocket-moved; "
                     "rm /rocket-moved; test ! -e /rocket-moved && "
                     "echo ROCKET_PROCESS_OK")
                wait("\nROCKET_PROCESS_OK\r\n")
                if "".join(text).replace("\r", "").splitlines().count("ROCKET_FILE_OK") != 2:
                    raise RuntimeError("ext4 write/read/rename did not preserve file contents")
                uart("nc -l -p 2222 -e /bin/sh")
                wait("MIKOS:TCP_LISTEN 2222")
                with socket.create_connection(("192.168.76.2", 2222), timeout=timeout) as client:
                    client.sendall(b"echo ROCKET_TCP_OK >/tcp-received\nexit\n")
                    while client.recv(4096):
                        pass
                uart("cat /tcp-received")
                wait("ROCKET_TCP_OK")
            else:
                wait("MIKOS:TCP_LISTEN 22")
                dropbear = ROOT / "build/tests/busybox/dropbear-host"
                client_command = [str(dropbear / "dbclient"), "-i",
                    str(dropbear / "mikos_ssh_key.dropbear"), "-c", "none", "-y", "-y",
                    "root@192.168.76.2"]
                # Give dbclient a local PTY, then type exit only after the
                # remote interactive shell has actually parked at its prompt.
                start = len("".join(text))
                interactive = client_command[:-1] + ["-t", client_command[-1]]
                with (log_path.parent / "ssh-logout.log").open("wb") as client_log:
                    client = subprocess.Popen(
                        ["script", "-qefc", shlex.join(interactive), "/dev/null"],
                        stdin=subprocess.PIPE, stdout=client_log,
                        stderr=subprocess.STDOUT, start_new_session=True)
                    try:
                        wait("MIKOS:PTY_CHILD_PARK child=", start)
                        client.communicate(b"exit\n", timeout=timeout)
                        if client.returncode:
                            raise RuntimeError("SSH logout failed; see ssh-logout.log")
                    finally:
                        if client.poll() is None:
                            os.killpg(client.pid, 9)
                            client.wait()
                if proc.poll() is not None:
                    raise RuntimeError("SSH logout stopped the simulator")
                # A new connection proves the listener survived the logout.
                result = subprocess.run(client_command + ["echo ROCKET_SSH_OK"],
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
                (log_path.parent / "ssh-client.log").write_bytes(result.stdout)
                if result.returncode or b"ROCKET_SSH_OK" not in result.stdout:
                    raise RuntimeError("SSH command failed; see ssh-client.log")
                uart("echo ROCKET_UART_AFTER_SSH")
                wait("\nROCKET_UART_AFTER_SSH\r\n")
            uart("exit")
            if proc.wait(timeout=timeout) != 0:
                raise RuntimeError("simulator failed")
            reader.join(5)
            if "MIKOS:EXIT 0" not in "".join(text):
                raise RuntimeError("guest did not exit successfully")
            print(f"PASS: Rocket {args.mode}; log: {log_path}")
        finally:
            if proc.poll() is None:
                proc.terminate()
                try: proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait()
            reader.join(5)
            Path(f"/tmp/mikos-rocket-net-{proc.pid}.sock").unlink(missing_ok=True)


if __name__ == "__main__":
    main()
