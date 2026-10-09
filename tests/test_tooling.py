#!/usr/bin/env -S uv run --locked
"""Dependency checks must fail before sockets/capture files are created."""

import argparse
import importlib.util
import pathlib
import subprocess
import tempfile
import types
import unittest
from unittest.mock import patch

script = pathlib.Path(__file__).resolve().parents[1] / "tools" / "simulation_smoke.py"
spec = importlib.util.spec_from_file_location("simulation_smoke", script)
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


class DependencyChecks(unittest.TestCase):
    def options(self, root, **overrides):
        values = dict(build=str(root), guards_only=False, ffmpeg="ffmpeg", ffprobe="ffprobe")
        values.update(overrides)
        return types.SimpleNamespace(**values)

    def assert_error(self, options, expected):
        parser = argparse.ArgumentParser()
        with patch("sys.stderr") as stderr, self.assertRaises(SystemExit) as error:
            smoke.preflight(parser, options)
        self.assertEqual(error.exception.code, 2)
        self.assertIn(expected, "".join(call.args[0] for call in stderr.write.call_args_list))

    def test_missing_tools_before_side_effects(self):
        for missing in ("ffmpeg", "ffprobe"):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)

                def lookup(name, missing=missing):
                    return None if name == missing else "/fake/" + pathlib.Path(name).name

                with (
                    patch.object(smoke.shutil, "which", side_effect=lookup),
                    patch.object(smoke.subprocess, "run") as run,
                ):
                    self.assert_error(self.options(root), f"Cannot find {missing}")
                    run.assert_not_called()
                    self.assertEqual(list(root.iterdir()), [])

    def test_missing_application(self):
        with patch.object(smoke.shutil, "which", return_value=None):
            self.assert_error(self.options("/missing"), "cmake --build build -j")

    def test_guard_check_needs_no_media_tools(self):
        with (
            patch.object(smoke.shutil, "which", side_effect=lambda name: name) as which,
            patch.object(smoke.subprocess, "run") as run,
        ):
            self.assertEqual(
                smoke.preflight(
                    argparse.ArgumentParser(), self.options("/build", guards_only=True)
                ),
                pathlib.Path("/build"),
            )
            self.assertEqual(which.call_count, 2)
            run.assert_not_called()

    def test_custom_paths_and_encoder(self):
        options = self.options("/build", ffmpeg="/custom/encoder", ffprobe="/custom/probe")
        encoders = subprocess.CompletedProcess([], 0, " V....D libx264 H.264 encoder\n", "")
        with (
            patch.object(smoke.shutil, "which", side_effect=lambda name: name),
            patch.object(smoke.subprocess, "run", return_value=encoders) as run,
        ):
            smoke.preflight(argparse.ArgumentParser(), options)
            self.assertEqual(run.call_args_list[0].args[0][0], "/custom/probe")
            self.assertEqual(run.call_args_list[1].args[0][0], "/custom/encoder")

    def test_missing_encoder_and_broken_executable(self):
        with patch.object(smoke.shutil, "which", side_effect=lambda name: name):
            with patch.object(
                smoke.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")
            ):
                self.assert_error(self.options("/build"), "needs the libx264 encoder")
            with patch.object(smoke.subprocess, "run", side_effect=OSError("cannot execute")):
                self.assert_error(
                    self.options("/build"), "Cannot run the configured FFmpeg/FFprobe"
                )


if __name__ == "__main__":
    unittest.main()
