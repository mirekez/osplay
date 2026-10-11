#!/usr/bin/env python3
"""Check interactive network hints and argument forwarding without running RTL."""
import os
from pathlib import Path
import shlex
import shutil
import socket
import subprocess
import tempfile
import unittest

LAUNCHER = Path(__file__).resolve().with_name("rocket_interactive.sh")


class LauncherTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="rocket-hints-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / "osplay test"
        self.scripts = self.root / "tests/rocket"
        self.scripts.mkdir(parents=True)
        shutil.copyfile(LAUNCHER, self.scripts / LAUNCHER.name)
        (self.scripts / "paths.sh").write_text(
            "root=" + shlex.quote(str(self.root)) + "\n")
        (self.scripts / "run_rocket.sh").write_text(
            '#!/usr/bin/env bash\nprintf "SIM_SOCKET=%s\\n" "$ROCKET_ETH_SOCKET"\n')
        network = self.root / "tests/network"
        network.mkdir()
        (network / "tap.py").write_text("# Network checks have a separate real-TAP test.\n")
        self.socket_path = str(Path(self.tmp.name) / "custom net.sock")
        self.env = dict(os.environ, ROCKET_INTERACTIVE_TAP="tap-rocket-test",
                        ROCKET_ETH_SOCKET="/ignored/environment.sock")

    def launch(self):
        return subprocess.run(
            ["bash", str(self.scripts / LAUNCHER.name), "--no-build",
             "--tap-socket", self.socket_path], cwd="/tmp", env=self.env,
            stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=5)

    def check_hints(self, output):
        self.assertIn("tap-rocket-test 192.168.76.1/24", output)
        self.assertIn("ifconfig eth0 192.168.76.2 netmask 255.255.255.0 up", output)
        self.assertIn("ping -n -i 10 -W 120 192.168.76.2", output)
        self.assertIn("Dropbear starts automatically during boot", output)
        self.assertNotIn("  . /etc/profile", output)
        self.assertIn("OSPLAY:TCP_LISTEN 22", output)
        client = next(line for line in output.splitlines() if "dropbear-host/dbclient" in line)
        args = shlex.split(client)
        self.assertEqual(args[0], str(self.root / "build/tests/busybox/dropbear-host/dbclient"))
        self.assertEqual(args[2], str(self.root / "build/tests/busybox/dropbear-host/osplay_ssh_key.dropbear"))
        self.assertEqual(args[-1], "root@192.168.76.2")
        self.assertIn("not yet validated end to end", output)

    def test_missing_bridge_setup_preserves_overrides(self):
        result = self.launch()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.check_hints(result.stderr)
        setup = next(line for line in result.stderr.splitlines() if "sudo env" in line)
        self.assertEqual(shlex.split(setup), [
            "sudo", "env", "OSPLAY_TAP=tap-rocket-test",
            "OSPLAY_TAP_SOCKET=" + self.socket_path, "bash",
            "tests/network/start_tap.sh", "--background"])
        self.assertNotIn("SIM_SOCKET=", result.stdout)

    def test_connected_launcher_prints_hints_and_forwards_socket(self):
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as peer:
            peer.bind(self.socket_path)
            result = self.launch()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.check_hints(result.stderr)
        self.assertIn("Type 'exit'", result.stderr)
        self.assertEqual(result.stdout, "SIM_SOCKET=" + self.socket_path + "\n")


if __name__ == "__main__":
    unittest.main()
