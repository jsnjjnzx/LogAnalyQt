import struct
import unittest
from pathlib import Path


ICON_PATH = Path(__file__).resolve().parents[1] / "assets" / "logo.icns"


def icns_chunk_types(path):
    data = path.read_bytes()
    if data[:4] != b"icns":
        raise ValueError("not an ICNS file")

    chunk_types = set()
    offset = 8
    while offset < len(data):
        chunk_type = data[offset : offset + 4]
        chunk_size = struct.unpack(">I", data[offset + 4 : offset + 8])[0]
        chunk_types.add(chunk_type)
        offset += chunk_size
    return chunk_types


class MacOSAppIconTest(unittest.TestCase):
    def test_icon_contains_standard_and_retina_sizes(self):
        required = {
            b"ic07",
            b"ic08",
            b"ic09",
            b"ic10",
            b"ic11",
            b"ic12",
            b"ic13",
            b"ic14",
        }
        self.assertTrue(required.issubset(icns_chunk_types(ICON_PATH)))


if __name__ == "__main__":
    unittest.main()
