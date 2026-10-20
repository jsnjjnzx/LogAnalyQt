"""Loopback-only SSH test peer; never invokes a system shell or real user database."""
import base64
import hashlib
import json
import logging
from pathlib import Path
import socket
import threading
import paramiko

USERNAME = "loganalyqt-test"
PASSWORD = "synthetic-loopback-password"


class LoopbackSshServer:
    def __init__(self, directory=None):
        self.directory = Path(directory) if directory is not None else None
        self.key = paramiko.RSAKey.generate(2048)
        self.fingerprint = "SHA256:" + base64.b64encode(hashlib.sha256(self.key.asbytes()).digest()).decode().rstrip("=")
        self.stop = threading.Event()
        self.lock = threading.RLock()
        self.transports = []
        self.sockets = []
        self.stats = {"connections": 0, "authAttempts": 0, "authAccepted": 0, "pty": 0, "shell": 0, "drops": 0, "echoes": []}
        logging.getLogger("paramiko").setLevel(logging.CRITICAL)
        self.listener = self.listen()
        self.stall_listener = self.listen()
        self.port = self.listener.getsockname()[1]
        self.stall_port = self.stall_listener.getsockname()[1]
        self.write_stats()

    @staticmethod
    def listen():
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(8)
        listener.settimeout(0.2)
        return listener

    def write_stats(self):
        if self.directory is None:
            return  # The throughput peer keeps all counters and generated logs in RAM.
        with self.lock:
            temporary = self.directory / "stats.new"
            temporary.write_text(json.dumps(self.stats), encoding="utf-8")
            temporary.replace(self.directory / "stats.json")

    def record(self, name, value=None):
        with self.lock:
            if value is None:
                self.stats[name] += 1
            else:
                self.stats[name].append(value)
            self.write_stats()

    def start(self):
        threading.Thread(target=self.accept, args=(self.listener, False), daemon=True).start()
        threading.Thread(target=self.accept, args=(self.stall_listener, True), daemon=True).start()

    def accept(self, listener, stall):
        while not self.stop.is_set():
            try:
                client, address = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            if address[0] != "127.0.0.1":
                client.close()
                continue
            self.sockets.append(client)
            if stall:
                continue  # Intentionally no SSH banner, only for cancellation during handshake.
            self.record("connections")
            threading.Thread(target=self.session, args=(client,), daemon=True).start()

    def session(self, client):
        peer = self

        class Interface(paramiko.ServerInterface):
            def __init__(self):
                self.shell = threading.Event()

            def get_allowed_auths(self, username):
                return "password"

            def check_auth_password(self, username, password):
                peer.record("authAttempts")
                if username == USERNAME and password == PASSWORD:
                    peer.record("authAccepted")
                    return paramiko.AUTH_SUCCESSFUL
                return paramiko.AUTH_FAILED

            def check_channel_request(self, kind, channel_id):
                return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

            def check_channel_pty_request(self, channel, term, width, height, pixel_width, pixel_height, modes):
                peer.record("pty")
                return True

            def check_channel_shell_request(self, channel):
                peer.record("shell")
                self.shell.set()
                return True

        transport = paramiko.Transport(client)
        self.transports.append(transport)
        try:
            transport.add_server_key(self.key)
            interface = Interface()
            transport.start_server(server=interface)
            channel = transport.accept(10)
            if channel is None or not interface.shell.wait(10):
                return
            channel.settimeout(0.2)
            channel.sendall(b"TEST_READY\r\n")
            self.serve_channel(channel, transport)
        except (EOFError, OSError, paramiko.SSHException):
            pass  # Interrupted negotiations/disconnects are deliberate cases, not successful sessions.
        finally:
            transport.close()
            client.close()

    def serve_channel(self, channel, transport):
        line = bytearray()
        while transport.is_active() and not self.stop.is_set():
            try:
                received = channel.recv(4096)
            except socket.timeout:
                continue
            if not received:
                break
            for byte in received:
                if byte == 0x15:
                    line.clear()
                elif byte == 13:
                    continue
                elif byte == 10:
                    command = line.decode("utf-8", errors="replace")
                    line.clear()
                    if command == "__DROP_TEST_SESSION__":
                        self.record("drops")
                        return
                    if command.startswith("echo "):
                        value = command[5:]
                        self.record("echoes", value)
                        channel.sendall(("TEST_ECHO:" + value + "\r\n").encode("utf-8"))
                    else:
                        channel.sendall(b"TEST_REJECTED\r\n")
                elif len(line) < 4096:
                    line.append(byte)
                else:
                    return

    def close(self):
        self.stop.set()
        self.listener.close()
        self.stall_listener.close()
        for transport in self.transports:
            transport.close()
        for client in self.sockets:
            client.close()
