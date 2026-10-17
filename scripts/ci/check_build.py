"""Validate the CI version and the complete downloadable package set."""
import argparse
import hashlib
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "release"))
from release_metadata import normalize_tag, project_version, expected_asset_names


def check_packages(directory, version, platform):
    names = expected_asset_names(version, "arm64")
    names = names[:2] if platform == "windows" else names[2:]
    for name in names:
        path = directory / name
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Missing or empty package: {name}")
        if name.endswith(".sha256"):
            package = directory / name.removesuffix(".sha256")
            if path.read_text().split()[0] != hashlib.sha256(package.read_bytes()).hexdigest():
                raise ValueError(f"SHA256 mismatch: {package.name}")
    if platform == "windows":
        with (directory / names[0]).open("rb") as executable:
            if executable.read(2) != b"MZ":
                raise ValueError("Not a Windows executable")
            executable.seek(0x3C)
            executable.seek(struct.unpack("<I", executable.read(4))[0])
            if executable.read(6) != b"PE\0\0\x64\x86":
                raise ValueError("Windows package must target x64")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--packages", type=Path)
    parser.add_argument("--platform", choices=["windows", "macos"])
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    version = project_version(root / "CMakeLists.txt")
    if os.environ.get("GITHUB_REF_TYPE") == "tag":
        if normalize_tag(os.environ["GITHUB_REF_NAME"]) != version:
            raise ValueError("Tag version does not match CMakeLists.txt")
    if args.packages:
        if not args.platform:
            parser.error("--packages requires --platform")
        check_packages(args.packages, version, args.platform)
    print(f"Validated LogAnalyQt {version}")


if __name__ == "__main__":
    main()
