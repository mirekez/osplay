"""Host link checks and copyable commands for the native Extreme console."""

from dataclasses import dataclass
import os
from pathlib import Path
import shlex
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "network"))
from tap import DEFAULT_SOCKET, check_link, select_tap

GUEST = "192.168.76.2"
MAC = "02:00:00:00:00:02"


@dataclass(frozen=True)
class InteractiveLink:
    tap: str
    media: str
    host: str

    @classmethod
    def from_environment(cls, media):
        return cls(select_tap(os.environ.get("MIKOS_TAP")), media, "192.168.76.1")

    def setup_hint(self, root, retry):
        environment = [f"MIKOS_TAP={self.tap}", f"MIKOS_TAP_SOCKET={self.media}"]
        commands = [
            ["bash", str(root / "tests/network/build_tap.sh")],
            [
                "sudo",
                "env",
                *environment,
                "bash",
                str(root / "tests/network/start_tap.sh"),
                "--background",
            ],
            [
                "env",
                *environment,
                "python3",
                str(root / "tests/network/tap.py"),
                "extreme",
            ],
            [
                "env",
                *environment,
                str(root / "tests/extreme/extreme_interactive.sh"),
                *retry,
            ],
        ]
        return (
            "Build, create or repair the shared link, check Extreme, then run without sudo:\n"
            + "\n".join("  " + shlex.join(command) for command in commands)
            + "\nSetup reuses an existing bridge. Run one simulator at a time.\n"
            "See tests/network/TAP.md. Use --no-network for UART only."
        )

    def check(self):
        check_link(self.tap, self.media, self.host, GUEST, MAC)

    def connection_hint(self, client, identity):
        ping = shlex.join(["ping", "-I", self.host, "-c", "2", "-W", "30", GUEST])
        ssh = shlex.join(
            [
                str(client),
                "-i",
                str(identity),
                "-c",
                "none",
                "-y",
                "-y",
                "-p",
                "22",
                f"root@{GUEST}",
            ]
        )
        return (
            f"Shared host link: {self.tap}, Extreme host {self.host}; guest: {GUEST}.\n"
            "Use one simulator at a time on the shared bridge.\n"
            "The guest IP is configured at boot; no ifconfig command or gateway is needed.\n"
            f"From another host terminal, check the link:\n  {ping}\n"
            "SSH after MIKOS:TCP_LISTEN 22 appears:\n"
            f"  {ssh}\n"
            "Use this Dropbear client and identity for the mikOS SSH test profile.\n"
            "Authentication can take several minutes. Reconnect after the previous client exits."
        )
