#!/usr/bin/env python3
"""Check shared identities and the authorized keys in a real Extreme rootfs."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(os.environ.get("EXTREME_BUILD_ROOT", ROOT / "build/tests/extreme"))


class SharedKeyTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="extreme-shared-key-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "mikos"
        for name in (
            "support/extreme/prepare_ssh_key.sh",
            "support/extreme/prepare_rootfs.sh",
            "support/toolchain.mk",
            "tests/busybox/Makefile",
            "tests/busybox/set_rootfs_owners.sh",
        ):
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        for name in ("tests/busybox/keys", "tests/busybox/rootfs"):
            shutil.copytree(ROOT / name, self.root / name)
        self.base = self.root / "build/tests/extreme"
        (self.base / "host").mkdir(parents=True)
        (self.base / "host/dropbearconvert").symlink_to(
            (BASE / "host/dropbearconvert").resolve()
        )
        self.key = self.root / "build/mikos_ssh_key"
        self.converted = (
            self.root / "build/tests/busybox/dropbear-host/mikos_ssh_key.dropbear"
        )
        self.environment = dict(os.environ, EXTREME_BUILD_ROOT=str(self.base))
        for name in (
            "EXTREME_ROOTFS_STAGE",
            "EXTREME_ROOTFS_IMAGE",
            "EXTREME_INIT_SCRIPT",
        ):
            self.environment.pop(name, None)

    def run_script(self, name):
        result = subprocess.run(
            ["bash", self.root / "support/extreme" / name],
            cwd="/tmp",
            env=self.environment,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr.decode())

    def public_key(self, key):
        return subprocess.check_output(
            ["ssh-keygen", "-y", "-f", key], text=True
        ).split()[:2]

    def test_clean_build_uses_shared_fixture_and_recreates_converted_key(self):
        self.run_script("prepare_ssh_key.sh")
        self.assertEqual(
            self.key.read_bytes(),
            (ROOT / "tests/busybox/keys/mikos_ssh_key").read_bytes(),
        )
        self.assertEqual(self.key.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.converted.stat().st_mode & 0o777, 0o600)
        original = self.converted.read_bytes()
        self.converted.unlink()
        self.run_script("prepare_ssh_key.sh")
        self.assertEqual(self.converted.read_bytes(), original)
        decoded = self.root / "decoded"
        subprocess.run(
            [
                self.base / "host/dropbearconvert",
                "dropbear",
                "openssh",
                self.converted,
                decoded,
            ],
            check=True,
            capture_output=True,
        )
        self.assertEqual(self.public_key(decoded), self.public_key(self.key))

    def test_existing_shared_identity_is_preserved_and_authorized_in_image(self):
        self.run_script("prepare_ssh_key.sh")
        old_converted = self.converted.read_bytes()
        self.key.unlink()
        self.key.with_suffix(".pub").unlink()
        subprocess.run(
            ["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", self.key],
            check=True,
        )
        original = self.key.read_bytes()
        # An old Extreme-specific public key must no longer be included.
        (self.base / "test_key.pub").write_text("obsolete-extreme-key\n")
        self.run_script("prepare_rootfs.sh")
        self.assertEqual(self.key.read_bytes(), original)
        self.assertNotEqual(self.converted.read_bytes(), old_converted)
        authorized = subprocess.check_output(
            [
                "/usr/sbin/debugfs",
                "-R",
                "cat /root/.ssh/authorized_keys",
                self.base / "rootfs.ext4",
            ],
            text=True,
            stderr=subprocess.DEVNULL,
        )
        self.assertIn(" ".join(self.public_key(self.key)), authorized)
        self.assertNotIn("obsolete-extreme-key", authorized)
        metadata = subprocess.check_output(
            [
                "/usr/sbin/debugfs",
                "-R",
                "stat /root/.ssh/authorized_keys",
                self.base / "rootfs.ext4",
            ],
            text=True,
            stderr=subprocess.DEVNULL,
        )
        self.assertRegex(metadata, r"Mode:\s+0600")
        self.assertRegex(metadata, r"User:\s+0\s+Group:\s+0")


if __name__ == "__main__":
    unittest.main()
