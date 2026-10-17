"""Run the production SSH worker against a temporary Paramiko loopback peer."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import paramiko
from ssh_success_session_server import LoopbackSshServer


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path, help="independently built ssh_success_session_tests executable")
    arguments = parser.parse_args()
    binary = arguments.binary.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="loganalyqt-ssh-success-", dir="/private/tmp") as directory:
        server = LoopbackSshServer(directory)
        server.start()
        try:
            completed = subprocess.run([str(binary), str(server.port), str(server.stall_port), server.fingerprint,
                                        str(Path(directory) / "stats.json")], capture_output=True, text=True, timeout=45)
            print(completed.stdout, end="")
            print(completed.stderr, end="")
            if completed.returncode:
                return completed.returncode
            with server.lock:
                stats = json.loads(json.dumps(server.stats))
            expected = {"LOOPBACK_FIRST", "中文回显", "LOOPBACK_SECOND", "AUTO_RECONNECTED"}
            assert expected <= set(stats["echoes"]), stats
            assert stats["authAccepted"] == 3 and stats["authAttempts"] == 3, stats
            assert stats["pty"] >= 3 and stats["shell"] >= 3 and stats["drops"] == 2, stats
            print("PASS: independent Paramiko", paramiko.__version__, "server observed", json.dumps(stats, ensure_ascii=False))
            print("Scope: loopback/synthetic credentials/temporary pin file only; no native Keychain or real device acceptance")
            return 0
        finally:
            server.close()


if __name__ == "__main__":
    raise SystemExit(main())
