#!/usr/bin/env python3
"""Unit tests for the bounded qc2cpp acceptance process runner."""

import pathlib
import sys
import tempfile
import time
import unittest

from qc2cpp_process import (
    ProcessFailure,
    RunningProcess,
    parse_json_observation,
    run_process,
    wait_for_events,
)
from qc2cpp_acceptance import server_command


class ProcessRunnerTests(unittest.TestCase):
    def test_missing_event_fails(self):
        with self.assertRaisesRegex(ProcessFailure, "missing event"):
            wait_for_events([], ["loaded"])

    def test_wrong_event_order_fails(self):
        with self.assertRaisesRegex(ProcessFailure, "event order"):
            wait_for_events(["map:e1m2", "map:e1m1"], ["map:e1m1", "map:e1m2"])

    def test_temporary_output_is_owned_by_runner(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "run"
            output.mkdir()
            self.assertTrue(output.exists())

    def test_timeout_terminates_the_child_and_records_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "server.log"
            with self.assertRaisesRegex(ProcessFailure, "timed out"):
                run_process(
                    [sys.executable, "-c", "import time; print('started', flush=True); time.sleep(30)"],
                    output,
                    timeout=0.1,
                )
            self.assertIn("started", output.read_text())

    def test_early_exit_reports_return_code(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "server.log"
            with self.assertRaisesRegex(ProcessFailure, "early exit.*7"):
                run_process(
                    [sys.executable, "-c", "raise SystemExit(7)"],
                    output,
                    timeout=1,
                )

    def test_interactive_early_exit_preserves_startup_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "server.log"
            process = RunningProcess(
                [sys.executable, "-c", "print('startup failed', flush=True); raise SystemExit(7)"],
                output,
            )
            try:
                time.sleep(0.1)
                with self.assertRaisesRegex(ProcessFailure, "early exit"):
                    process.observe("snapshot", "qc2cpp_test_snapshot", timeout=1)
            finally:
                process.close()
            self.assertIn("startup failed", output.read_text())

    def test_prefixed_server_json_is_decoded(self):
        observation = parse_json_observation(
            '[2026-09-01 12:00:00] {"qc2cpp_test_snapshot":{"map":"e1m1"}}',
            "qc2cpp_test_snapshot",
        )
        self.assertEqual(observation["map"], "e1m1")

    def test_acceptance_selects_its_qw_game_directory(self):
        command = server_command(pathlib.Path("mvdsv"), pathlib.Path("base"), "native")
        self.assertEqual(command[:5], [str(pathlib.Path("mvdsv").resolve()), "-basedir",
            str(pathlib.Path("base").resolve()), "-game", "qw"])
        self.assertIn("4", command)


if __name__ == "__main__":
    unittest.main()
