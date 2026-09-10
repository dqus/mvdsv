#!/usr/bin/env python3
"""Exercise live QWSP monsters and their QCMS restore on stock e1m2."""

import argparse
import pathlib
import tempfile
import time

from qc2cpp_acceptance import (
    ProcessFailure,
    RunningProcess,
    assert_map_snapshot,
    prepare_game_directory,
    require_file,
    server_command,
)


def require_live_monster(observation, *, require_scheduled=True):
    if observation.get("ready") is not True:
        raise ProcessFailure(f"QWSP did not expose a live monster: {observation}")
    if not observation.get("classname", "").startswith("monster_"):
        raise ProcessFailure(f"QWSP selected a non-monster entity: {observation}")
    if observation.get("health", 0) <= 0 or observation.get("think", 0) == 0:
        raise ProcessFailure(f"QWSP monster lacks health or a think callback: {observation}")
    if require_scheduled and observation.get("nextthink", 0) <= observation.get("server_time", 0):
        raise ProcessFailure(f"QWSP monster is not between scheduled callbacks: {observation}")


def monster_signature(observation):
    return {
        key: observation[key]
        for key in ("slot", "classname", "health", "think", "nextthink")
    }


def player_signature(observation):
    player = observation.get("player", {})
    return {
        key: player[key]
        for key in ("slot", "edict_slot", "origin", "health", "items",
                    "ammo_shells", "ammo_nails", "ammo_rockets", "ammo_cells")
    }


def require_live_player(observation):
    player = observation.get("player", {})
    if player.get("ready") is not True:
        raise ProcessFailure(f"QWSP did not expose an active player: {observation}")
    if player.get("slot", -1) < 0 or player.get("edict_slot", 0) <= 0:
        raise ProcessFailure(f"QWSP player has no valid slot/edict: {observation}")
    if len(player.get("origin", [])) != 3:
        raise ProcessFailure(f"QWSP player has no position: {observation}")


def require_live_qwsp_state(observation, *, require_scheduled=True, player=False):
    require_live_monster(observation, require_scheduled=require_scheduled)
    if player:
        require_live_player(observation)


def assert_qwsp_float_equal(expected, actual, *, tolerance, field):
    if abs(expected - actual) > tolerance:
        raise ProcessFailure(
            f"QWSP {field} changed across restore: expected={expected}, actual={actual}, "
            f"tolerance={tolerance}")


def assert_qwsp_restore_state(expected, actual, *, player, context,
                              player_identity=True):
    require_live_qwsp_state(actual, require_scheduled=False, player=player)
    expected_monster = monster_signature(expected)
    actual_monster = monster_signature(actual)
    for key in ("slot", "classname", "think"):
        if actual_monster[key] != expected_monster[key]:
            raise ProcessFailure(
                f"{context} did not restore monster {key}: "
                f"expected={expected_monster}, actual={actual_monster}")
    assert_qwsp_float_equal(expected_monster["health"], actual_monster["health"],
        tolerance=0.001, field=f"{context} monster health")
    # Restoring a connected client completes asynchronous replication before
    # this observer can run.  The live scheduler may therefore dispatch one
    # ordinary 0.1-second QWSP monster think after the exact saved nextthink;
    # callback identity and a later dispatch are checked separately.
    for key in ("nextthink",):
        assert_qwsp_float_equal(expected_monster[key], actual_monster[key],
            tolerance=0.15, field=f"{context} monster {key}")
    assert_qwsp_float_equal(expected["server_time"], actual["server_time"],
        tolerance=0.15, field=f"{context} server time")
    assert_qwsp_float_equal(expected["killed_monsters"], actual["killed_monsters"],
        tolerance=0.001, field=f"{context} killed-monster count")
    if player:
        expected_player = player_signature(expected)
        actual_player = player_signature(actual)
        if (player_identity and (expected_player["slot"] != actual_player["slot"]
                or expected_player["edict_slot"] != actual_player["edict_slot"])):
            raise ProcessFailure(
                f"{context} did not restore player slot/edict: "
                f"expected={expected_player}, actual={actual_player}")
        for key in ("health", "items", "ammo_shells", "ammo_nails",
                    "ammo_rockets", "ammo_cells"):
            assert_qwsp_float_equal(expected_player[key], actual_player[key],
                tolerance=0.001, field=f"{context} player {key}")
        for axis, (expected_axis, actual_axis) in enumerate(
                zip(expected_player["origin"], actual_player["origin"])):
            assert_qwsp_float_equal(expected_axis, actual_axis,
                tolerance=1.0, field=f"{context} player origin[{axis}]")


def wait_qwsp_monster_progress(process, prior, *, timeout, player=False):
    return process.observe_until(
        "qc2cpp_test_qwsp_monster read", "qc2cpp_test_qwsp_monster",
        lambda observation: observation.get("think_dispatches", 0)
        > prior.get("think_dispatches", 0)
        and monster_signature(observation) != monster_signature(prior)
        and (not player or observation.get("player", {}).get("ready") is True),
        timeout=timeout)


def run_suite(server, artifacts, assets, output, mode):
    output.mkdir(parents=True, exist_ok=True)
    # QCX save paths are bounded by MVDSV's MAX_OSPATH.  Keep this real server
    # root short enough for the temporary `.sav.tmp` commit name; it is never
    # removed, and failures name its preserved log directory below.
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qwm-{mode[0]}-", dir="/tmp"))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode)
    process = RunningProcess(
        server_command(server, basedir, mode, start_map=False,
                       startup_command=("map", "e1m2")), run_root / "server.log")
    try:
        initial_snapshot = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
        assert_map_snapshot(initial_snapshot, "e1m2")

        saved_monster = process.observe_until(
            "qc2cpp_test_qwsp_monster capture", "qc2cpp_test_qwsp_monster",
            lambda observation: observation.get("ready") is True
            and observation.get("nextthink", 0) > observation.get("server_time", 0),
            timeout=12)
        require_live_qwsp_state(saved_monster)
        saved_signature = monster_signature(saved_monster)

        process.send("save qwsp-monster")
        saved_snapshot = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
        assert_map_snapshot(saved_snapshot, "e1m2")
        save_path = basedir / "qw" / "save" / "qwsp-monster.sav"
        if not save_path.is_file() or save_path.read_bytes()[:4] != b"QCMS":
            raise ProcessFailure("QWSP monster save did not produce a QCMS container")

        advanced_monster = wait_qwsp_monster_progress(process, saved_monster, timeout=12)
        require_live_qwsp_state(advanced_monster)

        before_load = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        process.send("load qwsp-monster")
        after_load = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
        if after_load.get("time", 0) >= before_load.get("time", 0):
            raise ProcessFailure(
                f"QWSP monster load did not restore server time: {before_load} -> {after_load}")
        assert_map_snapshot(after_load, "e1m2")

        restored_monster = process.observe(
            "qc2cpp_test_qwsp_monster read", "qc2cpp_test_qwsp_monster", timeout=8)
        # The command buffer processes `load` and this observation before the
        # next SV_Physics tick, so a restored callback can be due at this exact
        # point.  Its future execution is proved separately below.
        require_live_qwsp_state(restored_monster, require_scheduled=False)
        if monster_signature(restored_monster) != saved_signature:
            raise ProcessFailure(
                "QWSP monster fields or callback identity did not restore: "
                f"saved={saved_monster}, restored={restored_monster}")

        resumed_monster = wait_qwsp_monster_progress(process, advanced_monster, timeout=12)
        require_live_qwsp_state(resumed_monster)
    except ProcessFailure as error:
        raise ProcessFailure(f"{error}; server log preserved in {run_root}") from error
    finally:
        process.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("native", "wasm"), required=True)
    parser.add_argument("--server", type=pathlib.Path, required=True)
    parser.add_argument("--artifacts", type=pathlib.Path, required=True)
    parser.add_argument("--assets", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        require_file(args.server)
        run_suite(args.server, args.artifacts, args.assets, args.output, args.mode)
    except ProcessFailure as error:
        print(f"QWSP monster acceptance: {error}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
