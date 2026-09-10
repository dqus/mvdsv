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
        require_live_monster(saved_monster)
        saved_signature = monster_signature(saved_monster)

        process.send("save qwsp-monster")
        saved_snapshot = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
        assert_map_snapshot(saved_snapshot, "e1m2")
        save_path = basedir / "qw" / "save" / "qwsp-monster.sav"
        if not save_path.is_file() or save_path.read_bytes()[:4] != b"QCMS":
            raise ProcessFailure("QWSP monster save did not produce a QCMS container")

        advanced_monster = process.observe_until(
            "qc2cpp_test_qwsp_monster read", "qc2cpp_test_qwsp_monster",
            lambda observation: observation.get("think_dispatches", 0)
            > saved_monster.get("think_dispatches", 0)
            and monster_signature(observation) != saved_signature,
            timeout=12)
        require_live_monster(advanced_monster)

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
        require_live_monster(restored_monster, require_scheduled=False)
        if monster_signature(restored_monster) != saved_signature:
            raise ProcessFailure(
                "QWSP monster fields or callback identity did not restore: "
                f"saved={saved_monster}, restored={restored_monster}")

        resumed_monster = process.observe_until(
            "qc2cpp_test_qwsp_monster read", "qc2cpp_test_qwsp_monster",
            lambda observation: observation.get("think_dispatches", 0)
            > advanced_monster.get("think_dispatches", 0)
            and monster_signature(observation) != saved_signature,
            timeout=12)
        require_live_monster(resumed_monster)
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
