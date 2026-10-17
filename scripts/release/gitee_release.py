#!/usr/bin/env python3

import argparse
import json
import mimetypes
import os
import uuid
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlencode
from urllib.request import Request, urlopen


class ReleaseError(RuntimeError):
    pass


def _is_prerelease(release: dict) -> bool:
    value = release.get("prerelease", False)
    if isinstance(value, str):
        return value.strip().lower() == "true"
    return value is True


class GiteeReleaseClient:
    def __init__(
        self,
        owner: str,
        repo: str,
        token: str,
        base_url: str = "https://gitee.com/api/v5",
    ) -> None:
        if not token:
            raise ReleaseError("缺少 GITEE_RELEASE_TOKEN")
        self._token = token
        self._base_url = base_url.rstrip("/")
        self._repo_path = f"/repos/{quote(owner, safe='')}/{quote(repo, safe='')}"

    def _sanitize(self, message: str) -> str:
        return message.replace(self._token, "***")

    def _request(
        self,
        method: str,
        path: str,
        form: Optional[Dict[str, str]] = None,
        body: Optional[bytes] = None,
        headers: Optional[Dict[str, str]] = None,
        allow_not_found: bool = False,
    ):
        request_headers = {
            "Accept": "application/json",
            "Authorization": f"Bearer {self._token}",
            "User-Agent": "LogAnalyQt-release-automation",
        }
        if headers:
            request_headers.update(headers)
        if form is not None:
            body = urlencode(form).encode("utf-8")
            request_headers["Content-Type"] = "application/x-www-form-urlencoded"

        request = Request(
            f"{self._base_url}{path}",
            data=body,
            headers=request_headers,
            method=method,
        )
        try:
            with urlopen(request, timeout=120) as response:
                payload = response.read()
        except HTTPError as error:
            if allow_not_found and error.code == 404:
                return None
            detail = error.read().decode("utf-8", errors="replace")
            raise ReleaseError(
                self._sanitize(f"Gitee API 请求失败：HTTP {error.code} {detail}")
            ) from None
        except URLError as error:
            raise ReleaseError(
                self._sanitize(f"无法连接 Gitee API：{error.reason}")
            ) from None

        if not payload:
            return {}
        try:
            return json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ReleaseError(f"Gitee API 返回了无效 JSON：{error}") from None

    def get_release(self, tag: str):
        encoded_tag = quote(tag, safe="")
        return self._request(
            "GET",
            f"{self._repo_path}/releases/tags/{encoded_tag}",
            allow_not_found=True,
        )

    def prepare(
        self,
        tag: str,
        name: str,
        body: str,
        target_commitish: str,
    ):
        release = self.get_release(tag)
        fields = {
            "name": name,
            "body": body,
            "prerelease": "true",
        }
        if release is None:
            fields.update({"tag_name": tag, "target_commitish": target_commitish})
            return self._request("POST", f"{self._repo_path}/releases", form=fields)
        if not _is_prerelease(release):
            raise ReleaseError(f"{tag} 已经是正式 Release，拒绝覆盖")
        return self._request(
            "PATCH",
            f"{self._repo_path}/releases/{release['id']}",
            form=fields,
        )

    def list_assets(self, release_id: int) -> List[dict]:
        assets = self._request(
            "GET", f"{self._repo_path}/releases/{release_id}/attach_files"
        )
        if not isinstance(assets, list):
            raise ReleaseError("Gitee API 返回的附件列表格式无效")
        return assets

    def upload(self, tag: str, file_path: Path):
        release = self.get_release(tag)
        if release is None:
            raise ReleaseError(f"找不到 {tag} 对应的 Release")
        if not _is_prerelease(release):
            raise ReleaseError(f"{tag} 已经是正式 Release，拒绝替换附件")

        file_path = Path(file_path)
        if not file_path.is_file():
            raise ReleaseError(f"待上传附件不存在：{file_path}")
        for asset in self.list_assets(release["id"]):
            if asset.get("name") == file_path.name:
                self._request(
                    "DELETE",
                    f"{self._repo_path}/releases/{release['id']}/attach_files/{asset['id']}",
                )

        boundary = f"LogAnalyQt-{uuid.uuid4().hex}"
        mime_type = mimetypes.guess_type(file_path.name)[0] or "application/octet-stream"
        prefix = (
            f"--{boundary}\r\n"
            f'Content-Disposition: form-data; name="file"; filename="{file_path.name}"\r\n'
            f"Content-Type: {mime_type}\r\n\r\n"
        ).encode("utf-8")
        payload = prefix + file_path.read_bytes() + f"\r\n--{boundary}--\r\n".encode(
            "ascii"
        )
        return self._request(
            "POST",
            f"{self._repo_path}/releases/{release['id']}/attach_files",
            body=payload,
            headers={"Content-Type": f"multipart/form-data; boundary={boundary}"},
        )

    def finalize(self, tag: str, expected_assets: Iterable[str]):
        release = self.get_release(tag)
        if release is None:
            raise ReleaseError(f"找不到 {tag} 对应的 Release")
        actual = {asset.get("name") for asset in self.list_assets(release["id"])}
        missing = sorted(set(expected_assets) - actual)
        if missing:
            raise ReleaseError("Release 缺少附件：" + "、".join(missing))
        return self._request(
            "PATCH",
            f"{self._repo_path}/releases/{release['id']}",
            form={"prerelease": "false"},
        )


def _client(args) -> GiteeReleaseClient:
    return GiteeReleaseClient(
        args.owner,
        args.repo,
        os.environ.get("GITEE_RELEASE_TOKEN", ""),
        base_url=args.base_url,
    )


def main(argv: Sequence[str] = ()) -> int:
    parser = argparse.ArgumentParser(description="管理 LogAnalyQt Gitee Release")
    parser.add_argument("--owner", default="jsnjjnzx")
    parser.add_argument("--repo", default="LogAnalyQt")
    parser.add_argument("--base-url", default="https://gitee.com/api/v5")
    subparsers = parser.add_subparsers(dest="command", required=True)

    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("--tag", required=True)
    prepare_parser.add_argument("--name", required=True)
    prepare_parser.add_argument("--notes-file", required=True, type=Path)
    prepare_parser.add_argument("--target", required=True)

    upload_parser = subparsers.add_parser("upload")
    upload_parser.add_argument("--tag", required=True)
    upload_parser.add_argument("--file", required=True, type=Path)

    finalize_parser = subparsers.add_parser("finalize")
    finalize_parser.add_argument("--tag", required=True)
    finalize_parser.add_argument("--expect", action="append", required=True)

    args = parser.parse_args(argv or None)
    client = _client(args)
    if args.command == "prepare":
        body = args.notes_file.read_text(encoding="utf-8")
        release = client.prepare(args.tag, args.name, body, args.target)
        print(f"Gitee prerelease 已准备：{release['id']}")
    elif args.command == "upload":
        asset = client.upload(args.tag, args.file)
        print(f"附件已上传：{asset.get('name', args.file.name)}")
    else:
        client.finalize(args.tag, args.expect)
        print(f"Gitee Release 已正式发布：{args.tag}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ReleaseError as error:
        print(f"发布失败：{error}", file=os.sys.stderr)
        raise SystemExit(1)
