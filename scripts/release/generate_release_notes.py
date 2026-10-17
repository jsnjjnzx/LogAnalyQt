#!/usr/bin/env python3

import argparse
import subprocess
from pathlib import Path
from typing import List, Sequence, Tuple

from release_metadata import normalize_tag, project_version, render_release_notes


def _git(root: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip()


def _previous_tag(root: Path, tag: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), "describe", "--tags", "--abbrev=0", f"{tag}^"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return result.stdout.strip() if result.returncode == 0 else ""


def _commits(root: Path, revision_range: str) -> List[Tuple[str, str, str]]:
    raw = _git(
        root,
        "log",
        "--no-merges",
        "--format=%H%x1f%s%x1f%b%x1e",
        revision_range,
    )
    commits: List[Tuple[str, str, str]] = []
    for record in raw.split("\x1e"):
        fields = record.strip().split("\x1f", 2)
        if len(fields) >= 2:
            commits.append((fields[0], fields[1], fields[2] if len(fields) == 3 else ""))
    return commits


def generate(root: Path, tag: str) -> str:
    version = normalize_tag(tag)
    _git(root, "rev-parse", "--verify", f"refs/tags/{tag}")
    configured_version = project_version(root / "CMakeLists.txt")
    if configured_version != version:
        raise ValueError(
            f"标签版本 {version} 与 CMakeLists.txt 项目版本 {configured_version} 不一致"
        )

    previous = _previous_tag(root, tag)
    revision_range = f"{previous}..{tag}" if previous else tag
    return render_release_notes(tag, previous, _commits(root, revision_range))


def main(argv: Sequence[str] = ()) -> int:
    parser = argparse.ArgumentParser(description="生成 LogAnalyQt Release 变更记录")
    parser.add_argument("--tag", required=True, help="发布标签，例如 v3.1.0")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv or None)

    notes = generate(args.root.resolve(), args.tag)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(notes, encoding="utf-8")
    print(f"Release 变更记录已生成：{args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
