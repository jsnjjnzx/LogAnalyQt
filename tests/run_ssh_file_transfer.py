"""Exercise production transfer code against an isolated loopback SFTP peer (Paramiko)."""

import base64
import hashlib
import os
from pathlib import Path
import socketserver
import subprocess
import sys
import tempfile
import threading
import time

import paramiko


USER, PASSWORD = "transfer-test", 'p"a\\ss:word'


class Login(paramiko.ServerInterface):
    def check_auth_password(self, username, password):
        self.attempts += 1
        return (paramiko.AUTH_SUCCESSFUL if (username, password) == (USER, PASSWORD)
                else paramiko.AUTH_FAILED)

    def check_channel_request(self, kind, channel_id):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def __init__(self):
        self.attempts = 0


class Folder(paramiko.SFTPServerInterface):
    def __init__(self, server, root):
        super().__init__(server)
        self.root = root

    def path(self, name):
        path = (self.root / name.lstrip("/")).resolve()
        if not path.is_relative_to(self.root):
            raise PermissionError(name)
        return path

    def list_folder(self, path):
        return [paramiko.SFTPAttributes.from_stat(file.stat(), file.name) for file in self.path(path).iterdir()]

    def stat(self, path):
        try:
            return paramiko.SFTPAttributes.from_stat(self.path(path).stat())
        except OSError as error:
            return paramiko.SFTPServer.convert_errno(error.errno)

    lstat = stat

    def open(self, path, flags, attr):
        write = bool(flags & (os.O_WRONLY | os.O_RDWR))
        with_fd = os.open(self.path(path), flags, 0o600)
        class SlowHandle(paramiko.SFTPHandle):
            def read(self, offset, length):
                time.sleep(0.04)
                return super().read(offset, length)

        handle = SlowHandle(flags) if path.endswith("/cancel.bin") else paramiko.SFTPHandle(flags)
        handle.readfile = handle.writefile = os.fdopen(with_fd, "wb" if write else "rb")
        return handle

    def rename(self, old, new):
        if self.path(new).exists():
            return paramiko.SFTP_FAILURE
        self.path(old).rename(self.path(new))
        return paramiko.SFTP_OK

    def mkdir(self, path, attr):
        self.path(path).mkdir()
        return paramiko.SFTP_OK

    def remove(self, path):
        self.path(path).unlink()
        return paramiko.SFTP_OK


def sftp(binary, root):
    key = paramiko.RSAKey.generate(2048)
    fingerprint = "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
    logins = []

    class Client(socketserver.BaseRequestHandler):
        def handle(self):
            transport = paramiko.Transport(self.request)
            transport.add_server_key(key)
            transport.set_subsystem_handler("sftp", paramiko.SFTPServer, Folder, root)
            login = Login()
            logins.append(login)
            try:
                transport.start_server(server=login)
                while transport.is_active():
                    transport.join(0.1)
            except (EOFError, OSError, paramiko.SSHException):
                pass
            finally:
                transport.close()

    class Server(socketserver.ThreadingTCPServer):
        daemon_threads = True

    with Server(("127.0.0.1", 0), Client) as server:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            env = {**os.environ, "QT_QPA_PLATFORM": "offscreen"}
            subprocess.run([binary, "sftp", str(server.server_address[1]), fingerprint],
                           check=True, timeout=90, env=env)
        finally:
            server.shutdown()
    assert [login.attempts for login in logins[:3]] == [0, 0, 1]
    assert logins[-1].attempts == 0


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="loganalyqt-file-transfer-") as directory:
        remote = Path(directory).resolve()
        (remote / "sub folder").mkdir()
        (remote / "sub folder" / "seed.txt").write_text("seed", encoding="utf-8")
        (remote / "sub folder" / "cancel.bin").write_bytes(b"x" * 2_000_000)
        sftp(executable, remote)
        assert not list(remote.rglob("*.part")), "remote staging file was left behind"
