"""Prove a managed broker exits when its owning process crashes (both platforms)."""
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time


def parent(helper, directory):
    process = subprocess.Popen(
        [helper, "-c", "broker.conf"], cwd=directory,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        env=dict(os.environ, LOGANALYQT_BROKER_MANAGED="1"),
    )
    for line in process.stdout:
        if b" running" in line:
            print(json.dumps({"pid": process.pid}), flush=True)
            break
    else:
        raise RuntimeError("Broker did not start")
    # Keep the pipe owned by this process until the test forcibly kills us.
    while True:
        time.sleep(1)


def process_alive(pid):
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(0x1000, False, pid)
        if not handle:
            return False
        code = wintypes.DWORD()
        try:
            return bool(kernel.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259
        finally:
            kernel.CloseHandle(handle)
    try:
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        return False


def main(helper):
    with tempfile.TemporaryDirectory(prefix="mqtt-parent-") as directory:
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        Path(directory, "broker.conf").write_text(
            f"listener {port} 127.0.0.1\nallow_anonymous true\npersistence false\nlog_dest stdout\n",
            encoding="utf-8",
        )
        owner = subprocess.Popen(
            [sys.executable, __file__, "--parent", str(Path(helper).resolve()), directory],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        result = queue.Queue()
        threading.Thread(target=lambda: result.put(owner.stdout.readline()), daemon=True).start()
        pid = None
        try:
            line = result.get(timeout=15)
            if not line:
                raise AssertionError("Owner failed: " + owner.stderr.read())
            pid = json.loads(line)["pid"]
            with socket.create_connection(("127.0.0.1", port), timeout=2):
                pass
            owner.kill()
            owner.wait(timeout=5)
            deadline = time.monotonic() + 6
            while process_alive(pid) and time.monotonic() < deadline:
                time.sleep(0.05)
            assert not process_alive(pid), "Broker survived its owner"
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", port))
            print("PASS: abrupt owner termination releases broker process and listener")
        finally:
            if owner.poll() is None:
                owner.kill()
                owner.wait(timeout=5)


if __name__ == "__main__":
    if sys.argv[1] == "--parent":
        parent(sys.argv[2], sys.argv[3])
    else:
        main(sys.argv[1])
