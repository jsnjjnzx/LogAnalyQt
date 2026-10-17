"""Validate the CI version and the complete downloadable package set."""
import argparse
import hashlib
import os
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "release"))
from release_metadata import normalize_tag, project_version, expected_asset_names


def check_macos_bundle(bundle):
    count = 0
    for path in bundle.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as source:
            magic = source.read(4)
        if magic not in (b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca"):
            continue
        count += 1
        architectures = subprocess.check_output(["lipo", "-archs", str(path)], text=True).split()
        if "arm64" not in architectures:
            raise ValueError(f"Missing arm64 architecture: {path}")
        libraries = subprocess.check_output(["otool", "-arch", "arm64", "-L", str(path)], text=True)
        for line in libraries.splitlines()[1:]:
            dependency = line.strip().split(" (", 1)[0]
            if dependency.startswith("/") and not dependency.startswith(("/System/", "/usr/lib/")):
                raise ValueError(f"External dependency in {path}: {dependency}")
    if not count:
        raise ValueError(f"No Mach-O files in {bundle}")
    print(f"Validated {count} bundled Mach-O files: arm64 and system/bundled dependencies")


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
    parser.add_argument("--macos-bundle", type=Path)
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
    if args.macos_bundle:
        check_macos_bundle(args.macos_bundle)
    print(f"Validated LogAnalyQt {version}")


if __name__ == "__main__":
    main()
