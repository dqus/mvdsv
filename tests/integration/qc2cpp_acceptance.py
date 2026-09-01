#!/usr/bin/env python3
"""Run the bounded qc2cpp map-lifecycle acceptance against a real MVDSV."""

import argparse
import os
import pathlib
import shutil
import socket
import sys
import tempfile
import time

from qc2cpp_process import (
    ProcessFailure,
    RunningProcess,
    run_client_acceptance,
    run_client_network_acceptance,
)


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


def prepare_client_directory(run_root, assets):
    base = run_root / "client"
    game_directory = base / "qw"
    game_directory.mkdir(parents=True)
    for pak_name in ("PAK0.PAK", "PAK1.PAK"):
        pak = assets / pak_name
        if pak.is_file():
            link_input(pak, game_directory / pak_name)
    return base


def assert_map_snapshot(snapshot, map_name):
    if snapshot.get("map") != map_name or snapshot.get("frame_count", 0) <= 2:
        raise ProcessFailure(f"map {map_name} did not reach a stable qc2cpp frame state: {snapshot}")


def assert_network_events(events, sessions=1, map_change=False):
    required = ("client_connect_count", "put_client_in_server_count",
        "client_prethink_count", "client_postthink_count", "client_kill_count",
        "client_disconnect_count")
    if any(events.get(key, 0) == 0 for key in required):
        raise ProcessFailure(f"network gameplay callbacks were not all observed: {events}")
    if events.get("player_health_after_kill", 0) <= 0:
        raise ProcessFailure(f"network gameplay did not respawn the player: {events}")
    if events.get("player_frags_after_kill") != -2:
        raise ProcessFailure(f"network gameplay did not execute the kill transition: {events}")
    if events.get("player_moved_after_spawn", 0) == 0:
        raise ProcessFailure(f"network movement was not acknowledged by the server: {events}")
    if events.get("legacy_game_entries") != 0:
        raise ProcessFailure(f"legacy game entry escaped qc2cpp dispatch: {events}")
    for key in ("client_connect_count", "client_kill_count", "client_disconnect_count"):
        if events.get(key, 0) < sessions:
            raise ProcessFailure(f"network gameplay did not complete {sessions} sessions: {events}")
    if map_change and events.get("normal_unpublish_count", 0) == 0:
        raise ProcessFailure(f"network gameplay did not survive a map change: {events}")
    owner = events.get("teledeath_owner", 0)
    toucher = events.get("teledeath_toucher", 0)
    if owner == 0 or owner != toucher or events.get("shared_self_after_put_client") != toucher:
        raise ProcessFailure(f"entity lifecycle corrupted the player self slot: {events}")


def assert_spectator_events(events):
    required = ("client_connect_count", "put_client_in_server_count",
        "client_disconnect_count", "spectator_put_client_in_server_count",
        "spectator_think_count")
    if any(events.get(key, 0) == 0 for key in required):
        raise ProcessFailure(f"spectator gameplay callbacks were not all observed: {events}")
    if events.get("legacy_game_entries") != 0:
        raise ProcessFailure(f"legacy game entry escaped qc2cpp dispatch: {events}")


def server_command(server, basedir, mode, port="0"):
    return [str(server.resolve()), "-basedir", str(basedir.resolve()), "-game", "qw", "-port", str(port),
        "+sv_progtype", "4" if mode == "native" else "5",
        "+sv_progsname", "game", "+deathmatch", "0", "+map", "e1m1"]


def client_command(client, basedir, port):
    return [str(client.resolve()), "-qc2cpp-acceptance", "-nosound",
        "-basedir", str(basedir.resolve()), "-game", "qw",
        "+connect", f"127.0.0.1:{port}"]


def network_client_command(client, basedir, port, spectator=False):
    command = [str(client.resolve()), "-qc2cpp-network-acceptance", "-nosound",
        "-basedir", str(basedir.resolve()),
        "-game", "qw"]
    if spectator:
        command += ["+spectator", "1"]
    return command + ["+connect", f"127.0.0.1:{port}"]


def available_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as socket_handle:
        socket_handle.bind(("127.0.0.1", 0))
        return socket_handle.getsockname()[1]


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


def run_client_suite(server, artifacts, assets, output, mode, client):
    output.mkdir(parents=True, exist_ok=True)
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qc2cpp-client-{mode}-", dir=output))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode)
    client_basedir = prepare_client_directory(run_root, assets)
    port = available_udp_port()
    process = RunningProcess(server_command(server, basedir, mode, port), run_root / "server.log")
    try:
        time.sleep(0.5)
        snapshot = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        assert_map_snapshot(snapshot, "e1m1")
        run_client_acceptance(
            client_command(client, client_basedir, port), run_root / "client.log", timeout=12)
    finally:
        process.close()


def run_network_suite(server, artifacts, assets, output, mode, client):
    """Keep a real client connected long enough to expose server gameplay faults."""
    output.mkdir(parents=True, exist_ok=True)
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qc2cpp-network-{mode}-"))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode)
    client_basedir = prepare_client_directory(run_root, assets)
    port = available_udp_port()
    process = RunningProcess(server_command(server, basedir, mode, port), run_root / "server.log")
    try:
        snapshot = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        assert_map_snapshot(snapshot, "e1m1")
        for session in range(2):
            run_client_network_acceptance(
                network_client_command(client, client_basedir, port),
                run_root / f"client-{session}.log", timeout=20)
        process.send("map e1m2")
        map_snapshot = process.observe_until(
            "qc2cpp_test_snapshot", "qc2cpp_test_snapshot",
            lambda snapshot: snapshot.get("map") == "e1m2", timeout=8)
        assert_map_snapshot(map_snapshot, "e1m2")
        run_client_network_acceptance(
            network_client_command(client, client_basedir, port), run_root / "client-map-change.log", timeout=20)
        process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        assert_network_events(
            process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8),
            sessions=3, map_change=True)
    finally:
        process.close()


def run_spectator_suite(server, artifacts, assets, output, mode, client):
    output.mkdir(parents=True, exist_ok=True)
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qc2cpp-spectator-{mode}-"))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode)
    client_basedir = prepare_client_directory(run_root, assets)
    port = available_udp_port()
    process = RunningProcess(server_command(server, basedir, mode, port), run_root / "server.log")
    try:
        snapshot = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        assert_map_snapshot(snapshot, "e1m1")
        run_client_network_acceptance(
            network_client_command(client, client_basedir, port, spectator=True),
            run_root / "client.log", timeout=20)
        assert_spectator_events(
            process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8))
    finally:
        process.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", choices=("map", "client", "network", "spectator"), required=True)
    parser.add_argument("--mode", choices=("native", "wasm"), required=True)
    parser.add_argument("--server", type=pathlib.Path, required=True)
    parser.add_argument("--artifacts", type=pathlib.Path, required=True)
    parser.add_argument("--assets", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--client", type=pathlib.Path)
    args = parser.parse_args()
    try:
        require_file(args.server)
        if args.suite == "map":
            run_map_suite(args.server, args.artifacts, args.assets, args.output, args.mode)
        else:
            if args.client is None:
                raise ProcessFailure(f"{args.suite} suite requires --client")
            require_file(args.client)
            if args.suite == "client":
                run_client_suite(
                    args.server, args.artifacts, args.assets, args.output, args.mode, args.client)
            elif args.suite == "network":
                run_network_suite(
                    args.server, args.artifacts, args.assets, args.output, args.mode, args.client)
            else:
                run_spectator_suite(
                    args.server, args.artifacts, args.assets, args.output, args.mode, args.client)
    except ProcessFailure as error:
        print(f"qc2cpp acceptance: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
