#!/usr/bin/env python3
"""Run the bounded qc2cpp map-lifecycle acceptance against a real MVDSV."""

import argparse
import os
import pathlib
import shutil
import sys
import tempfile
import time

from qc2cpp_process import ProcessFailure, RunningProcess


def require_file(path):
    if not path.is_file():
        raise ProcessFailure(f"required acceptance input is missing: {path}")


def link_input(source, destination):
    if destination.exists() or destination.is_symlink():
        destination.unlink()
    os.symlink(source, destination)


def prepare_game_directory(run_root, assets, artifact, mode):
    game_directory = run_root / "base" / "qw"
    game_directory.mkdir(parents=True)
    pak_found = False
    for pak_name in ("PAK0.PAK", "PAK1.PAK"):
        pak = assets / pak_name
        if pak.is_file():
            link_input(pak, game_directory / pak_name)
            pak_found = True
    maps = assets / "maps"
    if maps.is_dir():
        link_input(maps, game_directory / "maps")
    if not pak_found and not maps.is_dir():
        raise ProcessFailure(f"asset directory has neither PAK archives nor maps: {assets}")
    suffix = ".wasm" if mode == "wasm" else ".dylib" if sys.platform == "darwin" else ".so"
    require_file(artifact / f"game{suffix}")
    shutil.copy2(artifact / f"game{suffix}", game_directory / f"game{suffix}")
    return run_root / "base"


def assert_map_snapshot(snapshot, map_name):
    if snapshot.get("map") != map_name or snapshot.get("frame_count", 0) <= 2:
        raise ProcessFailure(f"map {map_name} did not reach a stable qc2cpp frame state: {snapshot}")


def server_command(server, basedir, mode):
    return [str(server.resolve()), "-basedir", str(basedir.resolve()), "-game", "qw", "-port", "0",
        "+sv_progtype", "4" if mode == "native" else "5",
        "+sv_progsname", "game", "+deathmatch", "1", "+map", "e1m1"]


def run_map_suite(server, artifacts, assets, output, mode):
    output.mkdir(parents=True, exist_ok=True)
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qc2cpp-{mode}-", dir=output))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode)
    command = server_command(server, basedir, mode)
    process = RunningProcess(command, run_root / "server.log")
    try:
        time.sleep(0.5)
        first = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        assert_map_snapshot(first, "e1m1")
        process.send("map e1m2")
        time.sleep(0.5)
        second = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        assert_map_snapshot(second, "e1m2")
        events = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=4)
        if events.get("normal_unpublish_count") != 1:
            raise ProcessFailure(f"expected one normal unpublish, got {events}")
        if events.get("legacy_game_entries") != 0:
            raise ProcessFailure(f"legacy game entry escaped qc2cpp dispatch: {events}")
        if events.get("gameplay_during_init") != 0:
            raise ProcessFailure(f"gameplay ran during qc2cpp init: {events}")
    finally:
        process.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", choices=("map",), required=True)
    parser.add_argument("--mode", choices=("native", "wasm"), required=True)
    parser.add_argument("--server", type=pathlib.Path, required=True)
    parser.add_argument("--artifacts", type=pathlib.Path, required=True)
    parser.add_argument("--assets", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        require_file(args.server)
        run_map_suite(args.server, args.artifacts, args.assets, args.output, args.mode)
    except ProcessFailure as error:
        print(f"qc2cpp acceptance: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
