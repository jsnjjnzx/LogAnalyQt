"""Resolve a tag to successful main-branch packages from the identical commit."""
import argparse
import json
import re
import subprocess
import time
from pathlib import Path


ARTIFACT_NAMES = {"LogAnalyQt-Windows-x64", "LogAnalyQt-macOS-arm64"}
PLATFORM_JOBS = {"Windows x64", "macOS arm64"}


def gh_json(endpoint):
    return json.loads(subprocess.check_output(["gh", "api", endpoint], text=True))


def select_run(runs, commit):
    matches = [run for run in runs if run["head_sha"] == commit
               and run["event"] == "push" and run["head_branch"] == "main"]
    if not matches:
        return None
    run = max(matches, key=lambda value: value["id"])
    if run["status"] != "completed":
        return None
    if run["conclusion"] != "success":
        raise ValueError("The identical main-branch commit has not passed CI")
    return run["id"]


def validate_packages(jobs, artifacts, run_id, commit):
    for name in PLATFORM_JOBS:
        matches = [job for job in jobs if job["name"] == name]
        if len(matches) != 1 or matches[0]["conclusion"] != "success":
            raise ValueError(f"Required platform verification did not pass: {name}")
    for name in ARTIFACT_NAMES:
        matches = [artifact for artifact in artifacts if artifact["name"] == name]
        if len(matches) != 1:
            raise ValueError(f"Missing or duplicate validated artifact: {name}")
        artifact = matches[0]
        origin = artifact["workflow_run"]
        if (artifact["expired"] or origin["id"] != run_id or origin["head_sha"] != commit
                or origin["head_branch"] != "main" or origin["repository_id"] != origin["head_repository_id"]):
            raise ValueError(f"Expired or mismatched artifact provenance: {name}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.repository) or not re.fullmatch(r"[0-9a-f]{40}", args.commit):
        parser.error("Expected owner/repository and the full tested commit SHA")
    base = f"repos/{args.repository}/actions"
    deadline = time.monotonic() + 180 * 60
    while True:
        runs = gh_json(f"{base}/workflows/build.yml/runs?head_sha={args.commit}&event=push&branch=main&per_page=100")["workflow_runs"]
        run_id = select_run(runs, args.commit)
        if run_id is not None:
            break
        if time.monotonic() >= deadline:
            raise TimeoutError("No successful main-branch build for this exact tag commit within 180 minutes")
        print(f"Waiting for the identical main-branch commit {args.commit} to finish CI", flush=True)
        time.sleep(30)
    jobs = gh_json(f"{base}/runs/{run_id}/jobs?filter=latest&per_page=100")["jobs"]
    artifacts = gh_json(f"{base}/runs/{run_id}/artifacts?per_page=100")["artifacts"]
    validate_packages(jobs, artifacts, run_id, args.commit)
    with args.output.open("a", encoding="utf-8") as output:
        output.write(f"build_run_id={run_id}\n")
    print(f"Reusing verified Windows and macOS packages from build {run_id}, commit {args.commit}", flush=True)


if __name__ == "__main__":
    main()
