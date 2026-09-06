#!/usr/bin/env python3
"""Run the bounded qc2cpp map-lifecycle acceptance against a real MVDSV."""

import argparse
import os
import pathlib
import shutil
import socket
import subprocess
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


def find_asset_file(directory, name):
    candidates = list(directory.iterdir())
    for candidate in candidates:
        if candidate.name == name and candidate.is_file():
            return candidate
    folded_name = name.casefold()
    for candidate in candidates:
        if candidate.name.casefold() == folded_name and candidate.is_file():
            return candidate
    return None


def prepare_game_directory(run_root, assets, artifact, mode, *, entity_override=False):
    game_directory = run_root / "base" / "qw"
    game_directory.mkdir(parents=True)
    pak_found = False
    for pak_name in ("PAK0.PAK", "PAK1.PAK"):
        pak = find_asset_file(assets, pak_name)
        if pak is not None:
            link_input(pak, game_directory / pak_name.lower())
            pak_found = True
    maps = assets / "maps"
    if entity_override:
        # The fatal/OOM fixture owns worldspawn and needs no stock entity
        # strings.  Retain the real BSP from the PAK while avoiding a second,
        # unrelated guest-allocation dependency from the stock e1m1 entity lump.
        (game_directory / "maps").mkdir()
        (game_directory / "maps" / "e1m1.ent").write_text(
            '{\n"classname" "worldspawn"\n}\n', encoding="ascii")
    elif maps.is_dir():
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
        pak = find_asset_file(assets, pak_name)
        if pak is not None:
            link_input(pak, game_directory / pak_name.lower())
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


def assert_saved_game_state(state, expected_think=None):
    if (state.get("parm16") != 101.0 or state.get("world_health") != 101.0
            or state.get("world_message") != "qcms-saved"
            or state.get("probe_slot", 0) == 0 or state.get("probe_think", 0) == 0):
        raise ProcessFailure(f"qc2cpp durable save state is incomplete: {state}")
    if expected_think is not None and state.get("probe_think") != expected_think:
        raise ProcessFailure(
            f"qc2cpp callback identity did not restore: expected {expected_think}, got {state}")


def assert_saved_callback_continues(process):
    """Drive the restored callback through one due-think transition."""
    process.send("qc2cpp_test_save_state trigger")
    time.sleep(0.15)
    after_trigger = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
    state = process.observe("qc2cpp_test_save_state read", "qc2cpp_test_save_state", timeout=8)
    if state.get("probe_think", 0) == 0 or state.get("probe_trigger_dispatches") != 1:
        raise ProcessFailure(
            "qc2cpp restored callback did not execute through the think scheduler: "
            f"snapshot={after_trigger}, state={state}")


def assert_spectator_events(events):
    required = ("client_connect_count", "put_client_in_server_count",
        "client_disconnect_count", "spectator_put_client_in_server_count",
        "spectator_think_count")
    if any(events.get(key, 0) == 0 for key in required):
        raise ProcessFailure(f"spectator gameplay callbacks were not all observed: {events}")
    if events.get("legacy_game_entries") != 0:
        raise ProcessFailure(f"legacy game entry escaped qc2cpp dispatch: {events}")


def server_command(server, basedir, mode, port="0", start_map=True, startup_command=None):
    command = [str(server.resolve()), "-basedir", str(basedir.resolve()), "-game", "qw", "-port", str(port),
        "+sv_progtype", "4" if mode == "native" else "5",
        "+sv_progsname", "game", "+deathmatch", "0"]
    if start_map:
        command += ["+map", "e1m1"]
    if startup_command is not None:
        command += [f"+{startup_command[0]}", *startup_command[1:]]
    return command


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


def connected_save_client_command(client, basedir, port):
    return [str(client.resolve()), "-qc2cpp-save-connected-acceptance", "-nosound",
        "-basedir", str(basedir.resolve()), "-game", "qw",
        "+connect", f"127.0.0.1:{port}"]


def available_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as socket_handle:
        socket_handle.bind(("127.0.0.1", 0))
        return socket_handle.getsockname()[1]


def require_log_marker(path, marker, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.is_file() and marker in path.read_text(encoding="utf-8"):
            return
        time.sleep(0.05)
    contents = path.read_text(encoding="utf-8") if path.is_file() else "<missing>"
    raise ProcessFailure(f"client did not observe {marker!r}: {contents}")


def run_map_suite(server, artifacts, assets, output, mode, expect_optional_fields=False,
                  expect_legacy_strings=False):
    output.mkdir(parents=True, exist_ok=True)
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qc2cpp-{mode}-", dir=output))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode)
    command = server_command(server, basedir, mode)
    process = RunningProcess(command, run_root / "server.log")
    try:
        time.sleep(0.5)
        first = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        assert_map_snapshot(first, "e1m1")
        references = process.observe("qc2cpp_test_entity_references",
            "qc2cpp_test_entity_references", timeout=8)
        expected_references = {"owner": 4, "enemy": 5, "groundentity": 6,
            "dmg_inflictor": 7, "newmis": 7}
        if references.get("ready") is not True or any(
                references.get(key) != value for key, value in expected_references.items()):
            raise ProcessFailure(
                f"QCX entity slots did not select their host edicts: {references}")
        if expect_optional_fields:
            optional_fields = process.observe("qc2cpp_test_optional_fields",
                "qc2cpp_test_optional_fields", timeout=8)
            if optional_fields.get("ready") is not True or optional_fields.get("hideentity") != 4:
                raise ProcessFailure(f"QCX optional entity fields failed: {optional_fields}")
        if expect_legacy_strings:
            legacy_strings = process.observe("qc2cpp_test_legacy_strings",
                "qc2cpp_test_legacy_strings", timeout=8)
            if legacy_strings.get("ready") is not True or legacy_strings.get("value") != "qcx-mutated":
                raise ProcessFailure(f"QCX legacy string borrow failed: {legacy_strings}")
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
        if events.get("server_state_bound_count") != 2:
            raise ProcessFailure(f"each spawned map must bind QCX server state: {events}")
        if events.get("gameplay_before_server_state") != 0:
            raise ProcessFailure(f"gameplay ran before QCX server state binding: {events}")
    finally:
        process.close()


def run_fatal_suite(server, artifacts, assets, output, mode):
    """A nested guest fatal must terminate before outer gameplay can resume."""
    output.mkdir(parents=True, exist_ok=True)
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qc2cpp-fatal-{mode}-", dir=output))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode,
                                    entity_override=True)
    server_log = run_root / "server.log"
    process = RunningProcess(server_command(server, basedir, mode), server_log)
    try:
        assert_map_snapshot(
            process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8), "e1m1")
        process.send("qc2cpp_test_fatal")
        if process.wait_for_exit(timeout=8) == 0:
            raise ProcessFailure("nested qc2cpp fatal did not terminate the server")
    finally:
        process.close()
    events = server_log.read_text(encoding="utf-8").splitlines()
    required = (
        "[qc2cpp-fatal] trigger",
        "[qc2cpp-fatal] outer-gameplay-entered",
        "[qc2cpp-fatal] nested-touch",
        "[qc2cpp-fatal] terminal-unpublish",
        "ERROR: SV_Error: qc2cpp fatal:",
    )
    for event in required:
        if not any(event in line for line in events):
            raise ProcessFailure(f"nested qc2cpp fatal did not emit {event!r}: {events}")
    if sum("[qc2cpp-fatal] terminal-unpublish" in line for line in events) != 1:
        raise ProcessFailure(f"nested qc2cpp fatal did not unpublish exactly once: {events}")
    if any("outer-gameplay-resumed" in line for line in events):
        raise ProcessFailure(f"nested qc2cpp fatal resumed outer gameplay: {events}")


def run_restore_oom_suite(server, artifacts, destination_artifacts, assets, output, mode):
    """A valid restore that exhausts guest heap storage is terminal after commit."""
    output.mkdir(parents=True, exist_ok=True)
    # Save/load still passes through legacy bounded server paths; keep both
    # disposable roots short enough that the terminal test observes restore,
    # rather than an unrelated filesystem truncation.
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qcoom-{mode}-", dir="/tmp"))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode,
                                    entity_override=True)
    source_log = run_root / "source.log"
    process = RunningProcess(server_command(server, basedir, mode), source_log)
    try:
        assert_map_snapshot(
            process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8), "e1m1")
        prepared = process.observe(
            "qc2cpp_test_restore_oom", "qc2cpp_test_restore_oom", timeout=8)
        if prepared.get("prepared") is not True:
            raise ProcessFailure(f"restore-oom input was not prepared: {prepared}")
        process.send("save qc2cpprestoreoom")
        process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=16)
        save_path = basedir / "qw" / "save" / "qc2cpprestoreoom.sav"
        if not save_path.is_file() or save_path.read_bytes()[:4] != b"QCMS":
            raise ProcessFailure("restore-oom source did not create a QCMS snapshot")
        snapshot_bytes = save_path.read_bytes()
    finally:
        process.close()

    destination_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qcoom-destination-{mode}-", dir="/tmp"))
    destination_basedir = prepare_game_directory(
        destination_root, assets, destination_artifacts, mode, entity_override=True)
    destination_save = destination_basedir / "qw" / "save" / "qc2cpprestoreoom.sav"
    destination_save.parent.mkdir(parents=True)
    destination_save.write_bytes(snapshot_bytes)
    server_log = destination_root / "server.log"
    destination = RunningProcess(server_command(
        server, destination_basedir, mode, start_map=False,
        startup_command=("load", "qc2cpprestoreoom")), server_log)
    try:
        if destination.wait_for_exit(timeout=12) == 0:
            raise ProcessFailure("post-commit qc2cpp restore OOM did not terminate the server")
    finally:
        destination.close()
    events = server_log.read_text(encoding="utf-8").splitlines()
    if sum("[qc2cpp-fatal] terminal-unpublish" in line for line in events) != 1:
        raise ProcessFailure(f"restore OOM did not unpublish exactly once: {events}")
    if not any("logical restore allocation failed" in line for line in events):
        raise ProcessFailure(f"restore OOM did not report the guest allocation diagnostic: {events}")


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


def run_save_suite(server, artifacts, assets, output, mode):
    """A qc2cpp server saves through the QCMS container, not the legacy path."""
    output.mkdir(parents=True, exist_ok=True)
    # MVDSV's legacy filesystem paths are bounded; keep the real server's
    # disposable game directory short just like the network acceptance suite.
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qc2cpp-save-{mode}-"))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode)
    process = RunningProcess(server_command(server, basedir, mode), run_root / "server.log")
    try:
        snapshot = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        assert_map_snapshot(snapshot, "e1m1")
        # The test-only probe writes one durable global, one durable entity scalar,
        # one arena-owned string and captures a real map callback identity.
        saved_game_state = process.observe(
            "qc2cpp_test_save_state save", "qc2cpp_test_save_state", timeout=8)
        assert_saved_game_state(saved_game_state)
        process.send("save qc2cpp-roundtrip")
        # Console commands are consumed by the server frame loop.  A subsequent
        # server-owned observation establishes that the preceding save command
        # has actually run before checking its filesystem effect.
        saved_snapshot = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
        assert_map_snapshot(saved_snapshot, "e1m1")
        save_path = basedir / "qw" / "save" / "qc2cpp-roundtrip.sav"
        if not save_path.is_file():
            raise ProcessFailure("qc2cpp save command did not create a save file")
        if save_path.read_bytes()[:4] != b"QCMS":
            raise ProcessFailure("qc2cpp save command did not create a QCMS container")
        mutated_game_state = process.observe(
            "qc2cpp_test_save_state mutate", "qc2cpp_test_save_state", timeout=8)
        if (mutated_game_state.get("parm16") != 202.0
                or mutated_game_state.get("world_health") != 202.0
                or mutated_game_state.get("world_message") != "qcms-mutated"
                or mutated_game_state.get("probe_think") != 0):
            raise ProcessFailure(f"qc2cpp save-state mutation did not apply: {mutated_game_state}")
        time.sleep(0.25)
        before_load = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        process.send("load qc2cpp-roundtrip")
        after_load = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
        if after_load.get("time", 0) >= before_load.get("time", 0):
            raise ProcessFailure(
                f"qc2cpp load did not restore the saved server time: {before_load} -> {after_load}")
        if after_load.get("globals_address") != saved_snapshot.get("globals_address"):
            raise ProcessFailure(
                f"qc2cpp load replaced published globals: {saved_snapshot} -> {after_load}")
        events = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8)
        if events.get("init_count") != 1:
            raise ProcessFailure(f"qc2cpp load replayed game initialization: {events}")
        restored_game_state = process.observe(
            "qc2cpp_test_save_state read", "qc2cpp_test_save_state", timeout=8)
        assert_saved_game_state(restored_game_state, saved_game_state["probe_think"])
        assert_saved_callback_continues(process)
        corrupt_path = basedir / "qw" / "save" / "qc2cpp-corrupt.sav"
        corrupt_path.write_bytes(b"QCMS\x01\x00")
        before_rejection = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        process.send("load qc2cpp-corrupt")
        after_rejection = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        if after_rejection.get("globals_address") != before_rejection.get("globals_address"):
            raise ProcessFailure(
                f"malformed QCMS changed published globals: {before_rejection} -> {after_rejection}")
        if after_rejection.get("time", 0) < before_rejection.get("time", 0):
            raise ProcessFailure(
                f"malformed QCMS changed server time: {before_rejection} -> {after_rejection}")
    finally:
        process.close()


def run_cross_save_suite(server, source_artifacts, destination_artifacts, assets, output,
                         source_mode, destination_mode):
    """Restore a client-free QCMS file in a separately booted transport."""
    output.mkdir(parents=True, exist_ok=True)
    source_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qc2cpp-save-source-{source_mode}-"))
    source_basedir = prepare_game_directory(source_root, assets, source_artifacts, source_mode)
    source = RunningProcess(
        server_command(server, source_basedir, source_mode), source_root / "server.log")
    try:
        assert_map_snapshot(
            source.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8), "e1m1")
        source_game_state = source.observe(
            "qc2cpp_test_save_state save", "qc2cpp_test_save_state", timeout=8)
        assert_saved_game_state(source_game_state)
        time.sleep(0.25)
        source.send("save qccross")
        source_saved = source.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
        assert_map_snapshot(source_saved, "e1m1")
        snapshot_path = source_basedir / "qw" / "save" / "qccross.sav"
        if not snapshot_path.is_file() or snapshot_path.read_bytes()[:4] != b"QCMS":
            raise ProcessFailure("source transport did not produce a QCMS snapshot")
        snapshot_bytes = snapshot_path.read_bytes()
    finally:
        source.close()

    destination_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qc2cpp-save-destination-{destination_mode}-"))
    destination_basedir = prepare_game_directory(
        destination_root, assets, destination_artifacts, destination_mode)
    destination_save = destination_basedir / "qw" / "save" / "qccross.sav"
    destination_save.parent.mkdir(parents=True)
    destination_save.write_bytes(snapshot_bytes)
    destination = RunningProcess(
        server_command(server, destination_basedir, destination_mode,
                       start_map=False, startup_command=("load", "qccross")),
        destination_root / "server.log")
    try:
        after_load = destination.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
        # The source and destination keep ticking while console commands and
        # observer requests are delivered.  The restored clock must therefore
        # continue from the saved epoch, rather than being a new-map clock.
        elapsed_after_save = after_load.get("time", 0) - source_saved.get("time", 0)
        if elapsed_after_save < 0 or elapsed_after_save > 0.5:
            raise ProcessFailure(
                "cross-transport restore did not continue from the saved server time: "
                f"source={source_saved}, after={after_load}")
        if after_load.get("globals_address") == 0:
            raise ProcessFailure(f"cross-transport restore did not publish globals: {after_load}")
        restored_game_state = destination.observe(
            "qc2cpp_test_save_state read", "qc2cpp_test_save_state", timeout=8)
        assert_saved_game_state(restored_game_state, source_game_state["probe_think"])
        assert_saved_callback_continues(destination)
        events = destination.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8)
        if events.get("init_count") != 1:
            raise ProcessFailure(f"cross-transport load replayed game initialization: {events}")
    finally:
        destination.close()


def run_connected_save_suite(server, artifacts, assets, output, mode, client):
    """A connected snapshot restores only while its original QW client remains live."""
    output.mkdir(parents=True, exist_ok=True)
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qcc-{mode}-", dir="/tmp"))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode)
    client_basedir = prepare_client_directory(run_root, assets)
    port = available_udp_port()
    process = RunningProcess(server_command(server, basedir, mode, port), run_root / "server.log")
    client_log = run_root / "client.log"
    client_process = None
    try:
        assert_map_snapshot(
            process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8), "e1m1")
        with client_log.open("w", encoding="utf-8") as output_file:
            client_process = subprocess.Popen(
                connected_save_client_command(client, client_basedir, port),
                stdout=output_file, stderr=subprocess.STDOUT, text=True)
            deadline = time.monotonic() + 12
            while True:
                if client_process.poll() is not None:
                    raise ProcessFailure("connected-save FTE client exited before becoming active")
                if time.monotonic() >= deadline:
                    raise ProcessFailure("connected-save FTE client did not become active")
                events = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=1)
                if (events.get("client_connect_count", 0) == 1
                        and events.get("put_client_in_server_count", 0) == 1):
                    break
                time.sleep(0.05)
            first_userid = events.get("last_client_userid", 0)
            if first_userid <= 0:
                raise ProcessFailure(f"connected-save client has no observable userid: {events}")
            process.send("save qc2cpp-connected")
            saved = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
            save_path = basedir / "qw" / "save" / "qc2cpp-connected.sav"
            if not save_path.is_file() or save_path.read_bytes()[:4] != b"QCMS":
                raise ProcessFailure("connected client did not produce a QCMS save")
            # Let the QW netchannel clear its prior reliable packet before the
            # restore queues the owner/client refresh.  The client remains
            # actively issuing movement commands throughout this interval.
            time.sleep(1.0)
            before_load = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
            process.send("load qc2cpp-connected")
            after_load = process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
            if after_load.get("time", 0) >= before_load.get("time", 0):
                raise ProcessFailure(
                    f"connected QCMS did not restore saved time: {before_load} -> {after_load}")
            if after_load.get("globals_address") != saved.get("globals_address"):
                raise ProcessFailure(
                    f"connected QCMS replaced published globals: {saved} -> {after_load}")
            events = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8)
            if (events.get("client_connect_count", 0) != 1
                    or events.get("put_client_in_server_count", 0) != 1
                    or events.get("init_count") != 1):
                raise ProcessFailure(f"connected QCMS replayed player or game startup: {events}")
            if (events.get("restore_replication_begin_count") != 1
                    or events.get("restore_replication_complete_count") != 1):
                raise ProcessFailure(f"connected QCMS did not rebuild replication: {events}")
            require_log_marker(
                client_log, "[qc2cpp-save-connected] replication", timeout=4)
            post_restore_prethink_count = events.get("client_prethink_count", 0)
            post_restore_postthink_count = events.get("client_postthink_count", 0)
        if client_process.wait(timeout=15) != 0:
            raise ProcessFailure("connected-save FTE client failed")
        if "[qc2cpp-save-connected] disconnect" not in client_log.read_text(encoding="utf-8"):
            raise ProcessFailure("connected-save FTE client did not complete its bounded disconnect")
        events = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8)
        if events.get("client_disconnect_count", 0) != 1:
            raise ProcessFailure(f"connected-save client disconnect was not observed: {events}")
        if (events.get("client_prethink_count", 0) <= post_restore_prethink_count
                or events.get("client_postthink_count", 0) <= post_restore_postthink_count):
            raise ProcessFailure(
                "connected QCMS restore did not resume real client gameplay callbacks")
        process.send("save qc2cpp-clientfree")
        client_free_path = basedir / "qw" / "save" / "qc2cpp-clientfree.sav"
        process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        if not client_free_path.is_file() or client_free_path.read_bytes()[:4] != b"QCMS":
            raise ProcessFailure("client-free QCMS replacement save was not created")
        before_rejected_load = process.observe(
            "qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        process.send("load qc2cpp-connected")
        after_rejected_load = process.observe(
            "qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        if after_rejected_load.get("time", 0) < before_rejected_load.get("time", 0):
            raise ProcessFailure(
                "a client-free replacement save re-authorized an old connected QCMS snapshot")

        # The authorization is snapshot-local, not a client identity token: a
        # new real client may occupy the same server slot but must not recover
        # the old client's image.
        reused_userid = process.observe(
            f"qc2cpp_test_reuse_userid {first_userid}",
            "qc2cpp_test_userid", timeout=8)
        if reused_userid.get("userid") != first_userid:
            raise ProcessFailure(f"server rejected userid reuse setup: {reused_userid}")
        second_client_log = run_root / "client-reconnect.log"
        with second_client_log.open("w", encoding="utf-8") as output_file:
            client_process = subprocess.Popen(
                connected_save_client_command(client, client_basedir, port),
                stdout=output_file, stderr=subprocess.STDOUT, text=True)
        deadline = time.monotonic() + 12
        while True:
            if client_process.poll() is not None:
                raise ProcessFailure("replacement FTE client exited before becoming active")
            if time.monotonic() >= deadline:
                raise ProcessFailure("replacement FTE client did not become active")
            events = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=1)
            if (events.get("client_connect_count", 0) == 2
                    and events.get("put_client_in_server_count", 0) == 2):
                break
            time.sleep(0.05)
        if events.get("last_client_userid") != first_userid:
            raise ProcessFailure(
                f"replacement client did not reuse the saved userid: {events}")
        before_reconnect_rejection = process.observe(
            "qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        process.send("load qc2cpp-connected")
        after_reconnect_rejection = process.observe(
            "qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
        if after_reconnect_rejection.get("time", 0) < before_reconnect_rejection.get("time", 0):
            raise ProcessFailure(
                "a replacement client re-authorized an old connected QCMS snapshot")
        released = process.observe(
            "qc2cpp_test_release_connected_client",
            "qc2cpp_test_release_connected_client", timeout=8)
        if released.get("released") is not True:
            raise ProcessFailure("server did not release replacement connected-save client")
        if client_process.wait(timeout=15) != 0:
            raise ProcessFailure("replacement connected-save FTE client failed")
        events = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8)
        if events.get("client_disconnect_count", 0) != 2:
            raise ProcessFailure(f"replacement client disconnect was not observed: {events}")
    finally:
        if client_process is not None and client_process.poll() is None:
            client_process.terminate()
            client_process.wait(timeout=3)
        process.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", choices=("map", "fatal", "restore-oom", "client", "network", "spectator", "save", "cross-save", "save-connected"), required=True)
    parser.add_argument("--mode", choices=("native", "wasm"), required=True)
    parser.add_argument("--server", type=pathlib.Path, required=True)
    parser.add_argument("--artifacts", type=pathlib.Path, required=True)
    parser.add_argument("--destination-artifacts", type=pathlib.Path)
    parser.add_argument("--source-mode", choices=("native", "wasm"))
    parser.add_argument("--destination-mode", choices=("native", "wasm"))
    parser.add_argument("--assets", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--client", type=pathlib.Path)
    parser.add_argument("--expect-optional-fields", action="store_true")
    parser.add_argument("--expect-legacy-strings", action="store_true")
    args = parser.parse_args()
    try:
        require_file(args.server)
        if args.suite == "map":
            run_map_suite(args.server, args.artifacts, args.assets, args.output, args.mode,
                args.expect_optional_fields, args.expect_legacy_strings)
        elif args.suite == "fatal":
            run_fatal_suite(args.server, args.artifacts, args.assets, args.output, args.mode)
        elif args.suite == "restore-oom":
            if args.destination_artifacts is None:
                raise ProcessFailure("restore-oom requires destination artifacts")
            run_restore_oom_suite(args.server, args.artifacts, args.destination_artifacts,
                                  args.assets, args.output, args.mode)
        elif args.suite == "save":
            run_save_suite(args.server, args.artifacts, args.assets, args.output, args.mode)
        elif args.suite == "cross-save":
            if args.destination_artifacts is None or args.source_mode is None or args.destination_mode is None:
                raise ProcessFailure("cross-save requires source/destination modes and destination artifacts")
            run_cross_save_suite(args.server, args.artifacts, args.destination_artifacts,
                args.assets, args.output, args.source_mode, args.destination_mode)
        elif args.suite == "save-connected":
            if args.client is None:
                raise ProcessFailure("save-connected suite requires --client")
            require_file(args.client)
            run_connected_save_suite(
                args.server, args.artifacts, args.assets, args.output, args.mode, args.client)
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
