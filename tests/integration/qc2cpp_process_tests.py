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
    run_client_acceptance,
    run_client_network_acceptance,
    run_process,
    wait_for_events,
)
from qc2cpp_acceptance import (
    ProcessFailure as AcceptanceFailure,
    assert_network_events,
    assert_spectator_events,
    client_command,
    network_client_command,
    server_command,
)


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

    def test_client_success_requires_post_parse_and_received_frame_events(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "client.log"
            with self.assertRaisesRegex(ProcessFailure, "missing event"):
                run_client_acceptance(
                    [sys.executable, "-c", "print('connected', flush=True)"],
                    output,
                    timeout=1,
                )

    def test_client_success_requires_the_headless_renderer_observation(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "client.log"
            command = (
                "print('[qc2cpp-acceptance] parsed-qw-server'); "
                "print('[qc2cpp-acceptance] received-qw-frame')"
            )
            with self.assertRaisesRegex(ProcessFailure, "missing event"):
                run_client_acceptance([sys.executable, "-c", command], output, timeout=1)

    def test_network_client_requires_the_full_gameplay_progression(self):
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "client.log"
            command = "print('[qc2cpp-network] active'); print('[qc2cpp-network] forward')"
            with self.assertRaisesRegex(ProcessFailure, "missing event"):
                run_client_network_acceptance([sys.executable, "-c", command], output, timeout=1)

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

    def test_client_acceptance_requests_early_headless_renderer(self):
        command = client_command(
            pathlib.Path("fteqw"), pathlib.Path("client-base"), 27500)
        self.assertEqual(command[:2], [str(pathlib.Path("fteqw").resolve()),
            "-qc2cpp-acceptance"])
        self.assertNotIn("+set", command)
        self.assertIn("-nosound", command)
        self.assertEqual(command[-2:], ["+connect", "127.0.0.1:27500"])

    def test_network_client_requests_the_bounded_gameplay_mode(self):
        command = network_client_command(
            pathlib.Path("fteqw"), pathlib.Path("client-base"), 27500)
        self.assertEqual(command[:2], [str(pathlib.Path("fteqw").resolve()),
            "-qc2cpp-network-acceptance"])
        self.assertNotIn("-qc2cpp-acceptance", command)

    def test_spectator_client_sets_spectator_before_connecting(self):
        command = network_client_command(
            pathlib.Path("fteqw"), pathlib.Path("client-base"), 27500, spectator=True)
        self.assertLess(command.index("+spectator"), command.index("+connect"))
        self.assertEqual(command[command.index("+spectator") + 1], "1")

    def test_network_events_require_all_gameplay_callbacks(self):
        with self.assertRaisesRegex(AcceptanceFailure, "callbacks"):
            assert_network_events({"client_connect_count": 1, "legacy_game_entries": 0})

    def test_network_events_require_a_respawned_player_after_kill(self):
        events = {
            "client_connect_count": 1,
            "put_client_in_server_count": 1,
            "client_prethink_count": 1,
            "client_postthink_count": 1,
            "client_kill_count": 1,
            "client_disconnect_count": 1,
            "legacy_game_entries": 0,
            "teledeath_owner": 1,
            "teledeath_toucher": 1,
            "shared_self_after_put_client": 1,
            "player_health_after_kill": 0,
            "player_frags_after_kill": -2,
            "player_moved_after_spawn": 1,
        }
        with self.assertRaisesRegex(AcceptanceFailure, "respawn"):
            assert_network_events(events)

    def test_network_events_reject_a_noop_kill_callback(self):
        events = {
            "client_connect_count": 1,
            "put_client_in_server_count": 1,
            "client_prethink_count": 1,
            "client_postthink_count": 1,
            "client_kill_count": 1,
            "client_disconnect_count": 1,
            "legacy_game_entries": 0,
            "teledeath_owner": 1,
            "teledeath_toucher": 1,
            "shared_self_after_put_client": 1,
            "player_health_after_kill": 100,
            "player_frags_after_kill": 0,
            "player_moved_after_spawn": 1,
        }
        with self.assertRaisesRegex(AcceptanceFailure, "kill transition"):
            assert_network_events(events)

    def test_network_events_require_server_acknowledged_player_movement(self):
        events = {
            "client_connect_count": 1,
            "put_client_in_server_count": 1,
            "client_prethink_count": 1,
            "client_postthink_count": 1,
            "client_kill_count": 1,
            "client_disconnect_count": 1,
            "legacy_game_entries": 0,
            "teledeath_owner": 1,
            "teledeath_toucher": 1,
            "shared_self_after_put_client": 1,
            "player_health_after_kill": 100,
            "player_frags_after_kill": -2,
            "player_moved_after_spawn": 0,
        }
        with self.assertRaisesRegex(AcceptanceFailure, "movement"):
            assert_network_events(events)

    def test_network_events_require_each_reconnect_and_a_map_change(self):
        events = {
            "client_connect_count": 1,
            "put_client_in_server_count": 1,
            "client_prethink_count": 1,
            "client_postthink_count": 1,
            "client_kill_count": 1,
            "client_disconnect_count": 1,
            "legacy_game_entries": 0,
            "teledeath_owner": 1,
            "teledeath_toucher": 1,
            "shared_self_after_put_client": 1,
            "player_health_after_kill": 100,
            "player_frags_after_kill": -2,
            "player_moved_after_spawn": 1,
            "normal_unpublish_count": 0,
        }
        with self.assertRaisesRegex(AcceptanceFailure, "3 sessions"):
            assert_network_events(events, sessions=3, map_change=True)

    def test_spectator_events_require_the_spectator_guest_path(self):
        events = {
            "client_connect_count": 1,
            "put_client_in_server_count": 1,
            "client_disconnect_count": 1,
            "legacy_game_entries": 0,
            "spectator_put_client_in_server_count": 0,
            "spectator_think_count": 0,
        }
        with self.assertRaisesRegex(AcceptanceFailure, "spectator"):
            assert_spectator_events(events)

    def test_network_events_reject_a_lifecycle_rewritten_self_slot(self):
        events = {
            "client_connect_count": 1,
            "put_client_in_server_count": 2,
            "client_prethink_count": 1,
            "client_postthink_count": 1,
            "client_kill_count": 1,
            "client_disconnect_count": 1,
            "legacy_game_entries": 0,
            "teledeath_owner": 42,
            "teledeath_toucher": 1,
            "shared_self_after_put_client": 62,
            "player_health_after_kill": 100,
            "player_frags_after_kill": -2,
            "player_moved_after_spawn": 1,
        }
        with self.assertRaisesRegex(AcceptanceFailure, "self slot"):
            assert_network_events(events)


if __name__ == "__main__":
    unittest.main()
