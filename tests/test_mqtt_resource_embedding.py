"""Compile both embedded helpers together and verify their binary bytes survive."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main(cmake, compiler):
    script = Path(__file__).resolve().parents[1] / "cmake/bin2cpp.cmake"
    with tempfile.TemporaryDirectory(prefix="mqtt-embed-") as directory:
        root = Path(directory)
        source = root / "bytes.bin"
        source.write_bytes(bytes([0, 0x68, 0xFF, 0]))
        generated = []
        for prefix in ("updater_exe", "mqtt_broker"):
            output = root / (prefix + ".cpp")
            command = [cmake, f"-DINPUT_FILE={source}", f"-DOUTPUT_FILE={output}"]
            if prefix != "updater_exe":
                command.append(f"-DSYMBOL_PREFIX={prefix}")
            subprocess.run(command + ["-P", str(script)], check=True, capture_output=True)
            generated.append(str(output))
        check = root / "check.cpp"
        check.write_text("""
#include <cstddef>
extern const unsigned char updater_exe_data[], mqtt_broker_data[];
extern const size_t updater_exe_size, mqtt_broker_size;
int main() {
    const unsigned char expected[] = {0, 0x68, 0xff, 0};
    if (updater_exe_size != 4 || mqtt_broker_size != 4) return 1;
    for (size_t i = 0; i < 4; ++i)
        if (updater_exe_data[i] != expected[i] || mqtt_broker_data[i] != expected[i]) return 2;
    return 0;
}
""", encoding="utf-8")
        binary = root / ("check.exe" if os.name == "nt" else "check")
        subprocess.run([compiler, *generated, str(check), "-o", str(binary)] + (["-static"] if os.name == "nt" else []), check=True, capture_output=True)
        subprocess.run([str(binary)], check=True)
    print("PASS: updater and MQTT helper embedding retain exact binary bytes and distinct symbols")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
