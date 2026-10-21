"""Guard exact-commit package reuse and preserve test failures in streamed CI logs."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("find_validated_build", ROOT / "scripts/ci/find_validated_build.py")
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)
from tests.prot698_qml_tests import run_logged


class ValidatedBuildTests(unittest.TestCase):
    commit = "a" * 40

    def run_record(self):
        return dict(id=12, head_sha=self.commit, event="push", head_branch="main", status="completed", conclusion="success")

    def artifacts(self):
        return [dict(name=name, expired=False, workflow_run=dict(id=12, head_sha=self.commit, head_branch="main",
                    repository_id=1, head_repository_id=1)) for name in ci.ARTIFACT_NAMES]

    def jobs(self):
        return [dict(name=name, conclusion="success") for name in ci.PLATFORM_JOBS]

    def test_reuses_only_the_successful_identical_main_commit(self):
        self.assertEqual(ci.select_run([self.run_record()], self.commit), 12)
        for key, value in [("head_sha", "b" * 40), ("event", "workflow_dispatch"), ("head_branch", "other")]:
            run = self.run_record(); run[key] = value
            self.assertIsNone(ci.select_run([run], self.commit))
        ci.validate_packages(self.jobs(), self.artifacts(), 12, self.commit)

    def test_waits_for_pending_build_and_rejects_a_newer_failed_build(self):
        pending = self.run_record(); pending.update(status="in_progress", conclusion=None)
        self.assertIsNone(ci.select_run([pending], self.commit))
        failed = self.run_record(); failed.update(id=13, conclusion="failure")
        with self.assertRaisesRegex(ValueError, "not passed"):
            ci.select_run([self.run_record(), failed], self.commit)

    def test_all_platform_checks_and_artifacts_are_required(self):
        with self.assertRaisesRegex(ValueError, "platform"):
            ci.validate_packages(self.jobs()[:1], self.artifacts(), 12, self.commit)
        with self.assertRaisesRegex(ValueError, "artifact"):
            ci.validate_packages(self.jobs(), self.artifacts()[:1], 12, self.commit)
        bad_jobs = self.jobs(); bad_jobs[0]["conclusion"] = "failure"
        with self.assertRaisesRegex(ValueError, "platform"):
            ci.validate_packages(bad_jobs, self.artifacts(), 12, self.commit)

    def test_expired_cross_commit_cross_run_or_fork_artifacts_are_rejected(self):
        mutations = [("expired", True), ("id", 99), ("head_sha", "b" * 40),
                     ("head_branch", "other"), ("head_repository_id", 2)]
        for key, value in mutations:
            with self.subTest(key=key):
                artifacts = self.artifacts()
                if key == "expired": artifacts[0][key] = value
                else: artifacts[0]["workflow_run"][key] = value
                with self.assertRaisesRegex(ValueError, "provenance"):
                    ci.validate_packages(self.jobs(), artifacts, 12, self.commit)

    def test_duplicate_artifacts_are_rejected(self):
        artifacts = self.artifacts(); artifacts.append(artifacts[0])
        with self.assertRaisesRegex(ValueError, "duplicate"):
            ci.validate_packages(self.jobs(), artifacts, 12, self.commit)

    def test_tag_workflow_reuses_packages_and_branch_workflow_does_not_publish(self):
        build = (ROOT / ".github/workflows/build.yml").read_text()
        release = (ROOT / ".github/workflows/release.yml").read_text()
        self.assertNotIn("tags:", build)
        self.assertNotIn("github_release.py", build)
        self.assertIn("branches: [main]", build)
        self.assertIn("cancel-in-progress: true", build)
        self.assertIn("tags: ['v*']", release)
        self.assertIn("--commit \"$GITHUB_SHA\"", release)
        self.assertEqual(release.count("run-id: ${{ steps.verified.outputs.build_run_id }}"), 2)
        self.assertNotIn("package_macos.sh", release)
        self.assertNotIn("package_single.ps1", release)
        self.assertIn("verify-release.yml", release)

    def test_separate_build_phase_still_compiles_all_test_executables(self):
        windows = (ROOT / "scripts/package_single.ps1").read_text()
        self.assertIn('$TestsOption = if ($BuildTests) { "ON" } else { "OFF" }', windows)
        self.assertIn('-DLOGANALYQT_BUILD_PROTOCOL_TESTS="$TestsOption"', windows)
        macos = (ROOT / "scripts/package_macos.sh").read_text()
        self.assertIn('if [[ "${RUN_TESTS:-0}" == "1" ]]; then', macos)
        self.assertIn('-DLOGANALYQT_BUILD_PROTOCOL_TESTS="$TESTS_OPTION"', macos)

    def test_streamed_output_preserves_failure_code_and_warning_evidence(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            environment = dict(os.environ, LOGANALYQT_DIAGNOSTICS_DIR=directory)
            result = run_logged([sys.executable, "-c", "import sys; print('QWARN: fixture', flush=True); sys.exit(3)"],
                                cwd=ROOT, env=environment, log_name="test.log")
            self.assertEqual(result.returncode, 3)
            self.assertIn("QWARN: fixture", result.stdout)
            self.assertEqual((Path(directory) / "test.log").read_text(), result.stdout)

    def test_streamed_process_has_a_real_timeout_and_keeps_its_output(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            environment = dict(os.environ, LOGANALYQT_DIAGNOSTICS_DIR=directory)
            with self.assertRaises(subprocess.TimeoutExpired):
                run_logged([sys.executable, "-c", "import time; print('started', flush=True); time.sleep(5)"],
                           cwd=ROOT, env=environment, log_name="timeout.log", timeout=0.5)
            self.assertIn("started", (Path(directory) / "timeout.log").read_text())


if __name__ == "__main__":
    unittest.main()
