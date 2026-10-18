"""Normalize copied framework/dylib IDs before signing a macOS app bundle."""
from pathlib import Path
import subprocess
import sys


def normalize_ids(bundle):
    bundle = Path(bundle).resolve()
    frameworks = bundle / "Contents/Frameworks"
    count = 0
    for path in frameworks.rglob("*"):
        if not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(bundle):
            continue
        with path.open("rb") as source:
            magic = source.read(4)
        if magic not in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):
            continue
        ids = subprocess.check_output(["otool", "-D", str(path)], text=True).splitlines()[1:]
        if not ids or not ids[0].startswith("/"):
            continue
        identifier = "@rpath/" + path.relative_to(frameworks).as_posix()
        subprocess.run(["install_name_tool", "-id", identifier, str(path)], check=True, capture_output=True)
        count += 1
    print(f"Normalized {count} copied macOS library IDs")
    return count


if __name__ == "__main__":
    if sys.platform != "darwin":
        raise SystemExit("This helper requires macOS")
    normalize_ids(sys.argv[1])
