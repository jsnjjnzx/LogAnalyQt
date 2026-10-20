"""Real SSH + production QML benchmark; log payloads and samples stay in memory.

Only small temporary QML import/configuration files and the final JSON summary
touch disk. No generated log stream is written, replayed from disk or printed.
Requires Paramiko in the invoking interpreter (never installs dependencies).
"""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

from prot698_qml_tests import module
from ssh_success_session_server import LoopbackSshServer


PAYLOAD = " 中文监控 68 09 00 43 05 01 00 00 00 00 00 00 16 ".encode()


class PerformancePeer(LoopbackSshServer):
    def __init__(self, burst_first=False):
        super().__init__()  # No stats file, key file or log file.
        self.measurements = {}
        self.burst_first = burst_first

    def serve_channel(self, channel, transport):
        command = bytearray()
        while transport.is_active() and not self.stop.is_set():
            try:
                data = channel.recv(4096)
            except socket.timeout:
                continue
            if not data:
                return
            command.extend(data.replace(b"\x15", b""))
            if len(command) > 4096:
                return
            if b"\n" not in command:
                continue
            line, _, command = command.partition(b"\n")
            if not line.startswith(b"BENCH "):
                continue
            try:
                _, identity, rate, seconds = line.decode("ascii").split()
                identity, rate, seconds = int(identity), int(rate), int(seconds)
            except (UnicodeError, ValueError):
                return
            if not 0 <= identity < 3 or not 512 <= rate <= 32 * 1024 * 1024 or not 1 <= seconds <= 7200:
                return
            channel.settimeout(30)
            start = time.monotonic()
            count = max(1, rate * seconds // 512)
            sequence = 0
            longest_send = 0.0
            while sequence < count and not self.stop.is_set():
                batch = bytearray()
                timestamp = time.time_ns() // 1_000_000
                for _ in range(min(64, count - sequence)):
                    prefix = f"LQ|{identity:02d}|{sequence:012d}|{timestamp:013d}|".encode()
                    body = prefix + b"x" * (510 - len(prefix) - len(PAYLOAD)) + PAYLOAD
                    assert len(body) == 510
                    batch.extend(body + b"\r\n")
                    sequence += 1
                before = time.monotonic()
                channel.sendall(batch)
                longest_send = max(longest_send, time.monotonic() - before)
                nominal = sequence * 512 / rate
                # Send each five-second allocation in half a second, then idle.
                target = start + (int(nominal / 5) * 5 + nominal % 5 / 10
                                  if self.burst_first and identity == 0 else nominal)
                self.stop.wait(max(0, target - time.monotonic()))
            elapsed = time.monotonic() - start
            with self.lock:
                self.measurements[identity] = {
                    "sent_lines": sequence, "sent_bytes": sequence * 512,
                    "send_seconds": elapsed, "longest_send_ms": longest_send * 1000,
                }
            channel.sendall(f"DONE|{identity}|{sequence}\r\n".encode())
            channel.settimeout(0.2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--seconds", type=int, default=30)
    parser.add_argument("--rate", type=int, default=1024 * 1024, help="bytes/second per session")
    parser.add_argument("--sessions", type=int, choices=(1, 2, 3), default=1)
    parser.add_argument("--search", action="store_true")
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--full-app", action="store_true", help="measure the real application and protocol backend")
    parser.add_argument("--view-state", choices=("live", "paused", "background"), default="live")
    parser.add_argument("--qml-source", type=Path, help="frozen QML snapshot for an older benchmark binary")
    parser.add_argument("--output", type=Path, help="small statistics JSON only; never log payloads")
    parser.add_argument("--profile", type=Path, help="macOS aggregate stack sample, for diagnosis only")
    parser.add_argument("--progress", type=Path, help="small per-minute progress JSON; choose a RAM filesystem")
    parser.add_argument("--input-probe", action="store_true", help="measure actual command-field key handling under load")
    parser.add_argument("--interaction-probe", action="store_true", help="measure full-application actions through a synchronized frame")
    parser.add_argument("--burst-first", action="store_true", help="burst session zero at ten times its average rate")
    args = parser.parse_args()
    if args.input_probe and args.headless:
        parser.error("--input-probe requires the GUI")
    if args.full_app and (args.headless or args.sessions != 1 or args.qml_source):
        parser.error("--full-app requires one visible session with its embedded QML")
    if args.interaction_probe and (not args.full_app or args.input_probe or args.seconds < 30):
        parser.error("--interaction-probe requires --full-app, >= 30 seconds and no --input-probe")
    if args.profile and args.seconds < 30:
        parser.error("--profile requires >= 30 seconds for a steady-state sample")
    if args.view_state != "live" and (not args.full_app or args.interaction_probe):
        parser.error("non-live --view-state requires --full-app without interaction probing")
    if args.burst_first and (args.sessions != 3 or args.rate > 3 * 1024 * 1024):
        parser.error("--burst-first requires three sessions and an average rate <= 3 MiB/s")
    if not 1 <= args.seconds <= 7200 or not 512 <= args.rate <= 32 * 1024 * 1024:
        parser.error("seconds must be 1..7200 and rate 512..33554432")
    root = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix="loganalyqt-ssh-perf-") as temporary:
        imports = Path(temporary) / "imports"
        (imports / "LogAnalyQt").mkdir(parents=True)
        (imports / "LogAnalyQt/qmldir").write_text("module LogAnalyQt\n")
        module(imports / "LogAnalyQt/Next", "LogAnalyQt.Next", args.qml_source or root / "qml/Next")
        peer = PerformancePeer(args.burst_first)
        peer.start()
        try:
            command = [str(args.binary.resolve()), str(peer.port), peer.fingerprint, temporary,
                       str(root / "tests/ssh-performance/Benchmark.qml"), str(args.seconds),
                       str(args.rate), str(args.sessions), "1" if args.search else "0",
                       "1" if args.headless else "0"]
            if args.full_app:
                command = [command[0], "--ui-smoke", "--interactive", "--ssh-performance"] + command[1:]
            environment = os.environ.copy()
            environment.update(QT_QUICK_CONTROLS_STYLE="Basic", QML_DISABLE_DISK_CACHE="1")
            if args.full_app:
                environment["LOGANALYQT_TEST_HOME"] = str(Path(temporary) / "home")
                environment["LOGANALYQT_SSH_PERF_VIEW_STATE"] = args.view_state
            if args.progress:
                environment["LOGANALYQT_SSH_PERF_PROGRESS"] = str(args.progress.resolve())
            if args.input_probe:
                environment["LOGANALYQT_SSH_PERF_INPUT_PROBE"] = "1"
            if args.interaction_probe:
                environment["LOGANALYQT_SSH_PERF_INTERACTION_PROBE"] = "1"
            process = subprocess.Popen(command, cwd=temporary, env=environment, stdout=subprocess.PIPE,
                                       stderr=subprocess.PIPE, text=True)
            try:
                if args.profile:
                    time.sleep(12)  # Skip application startup and the first history fill.
                    memory_before = subprocess.run(["/usr/bin/vmmap", "-summary", str(process.pid)],
                                                   capture_output=True, text=True, timeout=15)
                    sample = subprocess.run(["/usr/bin/sample", str(process.pid), "15", "5", "-file", "/dev/stdout"],
                                            capture_output=True, text=True, timeout=30)
                    memory_after = subprocess.run(["/usr/bin/vmmap", "-summary", str(process.pid)],
                                                  capture_output=True, text=True, timeout=15)
                    args.profile.write_text(sample.stdout + sample.stderr + "\nMEMORY BEFORE\n" + memory_before.stdout
                                            + memory_before.stderr + "\nMEMORY AFTER\n" + memory_after.stdout
                                            + memory_after.stderr, encoding="utf-8")
                stdout, stderr = process.communicate(timeout=args.seconds * 4 + 60)
            except BaseException:
                process.kill(); process.communicate(); raise
            completed = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
            if completed.returncode:
                log = Path(temporary) / "home/Library/Logs/LogAnalyQt/log.txt"
                diagnostics = "\n".join(log.read_text().splitlines()[-40:]) if args.full_app and log.exists() else ""
                raise RuntimeError(f"benchmark exit {completed.returncode}: {completed.stdout}\n{completed.stderr}\npeer={peer.stats}\n{diagnostics}")
            result = json.loads(completed.stdout)
            result["diagnostic_only"] = args.profile is not None
            result["server"] = peer.measurements
            result["diagnostics"] = completed.stderr.splitlines()[:20]
            result["payload_storage"] = "RAM only; no payload files"
            result["source"] = "production SshTerminal and LiveLogDialog; isolated credentials and unrelated UI backend"
            if args.full_app:
                result["source"] = "complete application with real LogBackend and embedded production QML; isolated credentials"
                log = Path(temporary) / "home/Library/Logs/LogAnalyQt/log.txt"
                result["app_diagnostics_bytes"] = log.stat().st_size if log.exists() else 0
                result["app_warnings"] = ([line for line in log.read_text().splitlines()
                                           if any(mark in line for mark in (" WARN ", " CRIT ", " FATAL "))
                                           and "Populating font family aliases" not in line][:20] if log.exists() else [])
            result["transport"] = "encrypted loopback SSH, Paramiko server"
            result["burst_first"] = args.burst_first
            result["validated"] = (result["received_lines"] == sum(v["sent_lines"] for v in peer.measurements.values())
                                   and result["content_errors"] == 0 and result["overflow_signals"] == 0)
            if args.full_app:
                result["controlled_window"] = (result["window_samples"] > 0
                                                and result["window_active_samples"] == result["window_samples"]
                                                and result["window_exposed_samples"] == result["window_samples"])
                result["validated"] = result["validated"] and not result["app_warnings"]
            if args.input_probe:
                result["validated"] = (result["validated"] and result.get("input_response", {}).get("samples", 0) > 0
                                       and result["input_response"]["failed_keystrokes"] == 0)
            if args.interaction_probe:
                actions = result.get("interaction_response", {})
                names = ("input", "pause", "scroll", "resume", "search", "search_clear",
                         "page_away", "page_back", "select", "parse_698", "parse_close")
                result["validated"] = (result["validated"] and actions.get("failures", -1) == 0
                                       and all(actions.get(name, {}).get("samples", 0) >= 3 for name in names))
            if args.burst_first:
                result["validated"] = (result["validated"] and len(result.get("session_progress", [])) == 3
                                       and all(item["max_commit_gap_ms"] < 1000
                                               for item in result["session_progress"][1:]))
            output = json.dumps(result, ensure_ascii=False, indent=2)
            if args.output:
                args.output.write_text(output + "\n", encoding="utf-8")
            printable = {key: value for key, value in result.items() if key != "rss_samples"}
            if "first_600s" in printable:
                printable["first_600s"] = {key: value for key, value in printable["first_600s"].items() if key != "rss_samples"}
            print(json.dumps(printable, ensure_ascii=False, indent=2))
            return 0 if result["validated"] else 1
        finally:
            peer.close()


if __name__ == "__main__":
    raise SystemExit(main())
