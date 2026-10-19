"""Publish a complete, verified GitHub Release from a version-tag workflow."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from urllib.parse import quote

from generate_release_notes import generate
from release_metadata import expected_asset_names, normalize_tag, project_version

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ci"))
from check_build import check_packages


def gh(*arguments):
    result = subprocess.run(["gh", *arguments], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or "GitHub request failed")
    return result.stdout


def manifest(directory, tag, notes, repository):
    version = normalize_tag(tag)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Invalid repository")
    for platform in ("windows", "macos"):
        check_packages(directory, version, platform)
    base = f"https://github.com/{repository}/releases"
    assets = []
    for name in expected_asset_names(version, "arm64"):
        path = directory / name
        assets.append({"name": name, "size": path.stat().st_size,
                       "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                       "browser_download_url": f"{base}/download/{tag}/{quote(name)}"})
    return {"tag_name": tag, "body": notes, "html_url": f"{base}/tag/{tag}",
            "assets": assets}


def publish(directory, tag, notes, repository, commit):
    metadata = manifest(directory, tag, notes, repository)
    output = directory / "update.json"
    output.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    remote_commit = gh("api", f"repos/{repository}/commits/{tag}", "--jq", ".sha").strip()
    if remote_commit != commit:
        raise ValueError("Remote tag no longer matches the tested commit")
    releases = json.loads(gh("api", "--paginate", f"repos/{repository}/releases", "--slurp"))
    release = next((item for page in releases for item in page if item["tag_name"] == tag), None)
    if release and not release["draft"]:
        raise ValueError("Refusing to overwrite a published Release")
    notes_file = directory / "release-notes.md"
    notes_file.write_text(notes, encoding="utf-8")
    if release is None:
        gh("release", "create", tag, "--repo", repository, "--draft", "--verify-tag",
           "--title", f"通联工坊 {tag}", "--notes-file", str(notes_file))
    else:
        gh("release", "edit", tag, "--repo", repository, "--notes-file", str(notes_file))
    paths = [directory / asset["name"] for asset in metadata["assets"]] + [output]
    gh("release", "upload", tag, *map(str, paths), "--repo", repository, "--clobber")
    details = json.loads(gh("release", "view", tag, "--repo", repository, "--json", "apiUrl"))
    release_id = details["apiUrl"].rsplit("/", 1)[-1]
    if not release_id.isdecimal():
        raise ValueError("Invalid draft Release ID")
    uploaded = json.loads(gh("api", f"repos/{repository}/releases/{release_id}"))
    if not uploaded["draft"] or uploaded["tag_name"] != tag:
        raise ValueError("Release changed while verifying draft attachments")
    expected = {path.name: path for path in paths}
    if {asset["name"] for asset in uploaded["assets"]} != set(expected):
        raise ValueError("Release attachments are incomplete or unexpected; draft retained")
    for asset in uploaded["assets"]:
        path = expected[asset["name"]]
        digest = "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()
        if asset["size"] != path.stat().st_size or asset.get("digest") != digest:
            raise ValueError(f"Uploaded attachment failed verification: {path.name}; draft retained")
    gh("release", "edit", tag, "--repo", repository, "--draft=false", "--latest")
    print(f"Published https://github.com/{repository}/releases/tag/{tag}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--tag", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if os.environ.get("GITHUB_EVENT_NAME") != "push" or os.environ.get("GITHUB_REF") != f"refs/tags/{args.tag}":
        raise ValueError("Only a pushed version tag may publish a Release")
    if normalize_tag(args.tag) != project_version(root / "CMakeLists.txt"):
        raise ValueError("Tag and project version differ")
    notes = generate(root, args.tag)
    notes += "\nGitee 旧版用户请手动安装本版一次，后续版本从 GitHub 更新。macOS 使用 ad-hoc 签名，未经 Apple 公证。\n"
    publish(args.directory, args.tag, notes, os.environ["GITHUB_REPOSITORY"], os.environ["GITHUB_SHA"])


if __name__ == "__main__":
    main()
