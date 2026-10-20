"""Exercise stream boundaries/backpressure/completion through a RAM-only SSH peer."""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

from ssh_success_session_server import LoopbackSshServer


class StreamPeer(LoopbackSshServer):
    def serve_channel(self, channel, transport):
        line = bytearray()
        encoding = "utf-8"
        tabs = 0
        while transport.is_active() and not self.stop.is_set():
            try:
                received = channel.recv(16384)
            except socket.timeout:
                continue
            if not received:
                return
            for byte in received:
                if byte == 0x15:
                    line.clear(); tabs = 0
                elif byte == 9:
                    tabs += 1
                    if line == b"tai":
                        channel.sendall(b"l")
                    elif line == b"lo" and tabs >= 2:
                        channel.sendall(b"\r\nlock/  log/\r\nlo")
                    elif line == b"noisy":
                        channel.settimeout(10)
                        for _ in range(320):
                            channel.sendall(b"NOISY:" + b"n" * 1017 + b"\n")
                        channel.sendall(b"NOISY_END\n")
                        channel.settimeout(0.2)
                    elif line == b"blank" and tabs == 1:
                        channel.settimeout(10)
                        for _ in range(150): channel.sendall(b"\n" * 2048)
                        channel.sendall(b"BLANK_END\n"); channel.settimeout(0.2)
                    else:
                        channel.sendall(b"\x07")
                elif byte == 10:
                    command = line.decode(encoding, errors="replace"); line.clear(); tabs = 0
                    if command == "SPLIT":
                        payload = "分块中文😀\r\n\x1b[31mRED\x1b[0m\r\n\x1b]0;title\x1b\\END\n".encode()
                        for value in payload:
                            channel.sendall(bytes([value])); time.sleep(0.003)
                    elif command == "GB":
                        encoding = "gbk"; channel.sendall(b"GB_READY\n")
                    elif command == "UTF8":
                        encoding = "utf-8"; channel.sendall(b"UTF8_READY\n")
                    elif command.startswith("echo "):
                        channel.sendall((command[5:] + "\r\n").encode(encoding))
                    elif command == "LONG":
                        channel.settimeout(10)
                        channel.sendall(b"x" * 40000 + b" 68 04 07 00 00 00\nLONG_END\n")
                        channel.settimeout(0.2)
                    elif command == "PARTIAL":
                        channel.sendall(b"OLD_PARTIAL")
                    elif command == "AFTER_CLEAR":
                        channel.sendall(b"_OLD_REMAINDER\nNEW_AFTER_CLEAR\n")
                    elif command == "STDERR":
                        channel.send_stderr(b"STDERR_CONTENT\n"); channel.sendall(b"STDOUT_CONTENT\n")
                    elif command == "BURST":
                        channel.settimeout(30)
                        for start in range(0, 65536, 128):
                            # 8 MiB generated batch by batch; never materialize or save the full stream.
                            block = b"".join(f"B:{number:08d}:".encode() + b"b" * 116 + b"\n"
                                             for number in range(start, start + 128))
                            assert len(block) == 128 * 128
                            channel.sendall(block)
                        channel.sendall(b"BURST_END\n"); channel.settimeout(0.2)
                    elif command == "EOF":
                        channel.sendall(b"FINAL_WITHOUT_NEWLINE"); channel.shutdown_write()
                        time.sleep(0.1); return
                elif byte != 13:
                    line.append(byte)
                    if len(line) > 1024 * 1024:
                        return


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    args = parser.parse_args()
    peer = StreamPeer()
    peer.start()
    try:
        with tempfile.TemporaryDirectory(prefix="loganalyqt-ssh-stream-") as temporary:
            result = subprocess.run([str(args.binary.resolve()), str(peer.port), peer.fingerprint, temporary],
                                    cwd=temporary, env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                                    capture_output=True, text=True, timeout=90)
            print(result.stdout + result.stderr, end="")
            return result.returncode
    finally:
        peer.close()


if __name__ == "__main__":
    raise SystemExit(main())
