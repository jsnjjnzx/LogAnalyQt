import re
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple


_TAG_PATTERN = re.compile(r"^v(?P<version>\d+\.\d+\.\d+)$")
_PROJECT_VERSION_PATTERN = re.compile(
    r"project\s*\(\s*LogAnalyQt\s+VERSION\s+(\d+\.\d+\.\d+)",
    re.IGNORECASE,
)
_CONVENTIONAL_SUBJECT_PATTERN = re.compile(
    r"^(?P<type>[a-z]+)(?:\([^)]*\))?!?:\s*(?P<subject>.+)$",
    re.IGNORECASE,
)


def normalize_tag(tag: str) -> str:
    match = _TAG_PATTERN.fullmatch(tag.strip())
    if not match:
        raise ValueError("发布标签必须使用 vX.Y.Z 格式")
    return match.group("version")


def project_version(cmake_file: Path) -> str:
    content = Path(cmake_file).read_text(encoding="utf-8-sig")
    match = _PROJECT_VERSION_PATTERN.search(content)
    if not match:
        raise ValueError(f"无法从 {cmake_file} 读取 LogAnalyQt 项目版本")
    return match.group(1)


def expected_asset_names(version: str, mac_arch: str) -> List[str]:
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("版本号必须使用 X.Y.Z 格式")
    if not re.fullmatch(r"[A-Za-z0-9_]+", mac_arch):
        raise ValueError("macOS 架构名称只能包含字母、数字和下划线")

    windows = f"TonglianWorkshop_v{version}_Windows_x64.exe"
    mac_prefix = f"TonglianWorkshop_v{version}_macOS_{mac_arch}"
    return [
        windows,
        f"{windows}.sha256",
        f"{mac_prefix}.dmg",
        f"{mac_prefix}.dmg.sha256",
        f"{mac_prefix}_update.zip",
        f"{mac_prefix}_update.zip.sha256",
    ]


def _commit_group(subject: str) -> Tuple[str, str]:
    match = _CONVENTIONAL_SUBJECT_PATTERN.match(subject.strip())
    if not match:
        return "其他变更", subject.strip()

    commit_type = match.group("type").lower()
    title = match.group("subject").strip()
    if commit_type == "feat":
        return "新增功能", title
    if commit_type == "fix":
        return "问题修复", title
    if commit_type == "perf":
        return "性能优化", title
    return "其他变更", title


def render_release_notes(
    current_tag: str,
    previous_tag: str,
    commits: Iterable[Sequence[str]],
) -> str:
    version = normalize_tag(current_tag)
    groups = {
        "新增功能": [],
        "问题修复": [],
        "性能优化": [],
        "其他变更": [],
    }

    for commit in commits:
        commit_hash, subject = commit[0], commit[1]
        group, title = _commit_group(subject)
        groups[group].append(f"- {title} (`{commit_hash[:7]}`)")

    comparison = (
        f"{previous_tag}...{current_tag}" if previous_tag else current_tag
    )
    lines = [
        f"## 通联工坊 v{version}",
        "",
        f"变更范围：`{comparison}`",
    ]
    for group_name, entries in groups.items():
        if not entries:
            continue
        lines.extend(["", f"### {group_name}", "", *entries])
    if not any(groups.values()):
        lines.extend(["", "本版本没有可归档的提交记录。"])
    lines.append("")
    return "\n".join(lines)
