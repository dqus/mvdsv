#!/usr/bin/env python3
"""Accept QWSP QCMS restores through fresh maps and both QCX transports."""

import argparse
import pathlib
import subprocess
import tempfile
import time

from qc2cpp_acceptance import (
    ProcessFailure,
    available_udp_port,
    assert_map_snapshot,
    prepare_client_directory,
    prepare_game_directory,
    require_file,
    roster_client_command,
    server_command,
    wait_events,
)
from qc2cpp_process import RunningProcess
from qwsp_monster_acceptance import (
    assert_qwsp_restore_state,
    player_signature,
    require_live_qwsp_state,
    wait_qwsp_monster_progress,
)


def close_client(client_process):
    if client_process is None or client_process.poll() is not None:
        return
    client_process.terminate()
    try:
        client_process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        client_process.kill()
        client_process.wait()


def start_alice(client, basedir, port, log_path):
    with log_path.open("w", encoding="utf-8") as output:
        return subprocess.Popen(
            roster_client_command(client, basedir, port, name="Alice", team="red",
                                  acceptance=False),
            stdout=output, stderr=subprocess.STDOUT, text=True)


def wait_for_client_events(process, client_process, client_log, predicate, *, timeout,
                           description):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        if client_process.poll() is not None:
            contents = client_log.read_text(encoding="utf-8") if client_log.is_file() else "<missing>"
            raise ProcessFailure(f"{description}: FTE exited: {contents}")
        last = process.observe("qc2cpp_test_events", "qc2cpp_test_events",
            timeout=min(1, deadline - time.monotonic()))
        if predicate(last):
            return last
        time.sleep(0.05)
    raise ProcessFailure(f"{description}: {last}")


def capture_qwsp_state(process, *, player=True, timeout=12):
    state = process.observe_until(
        "qc2cpp_test_qwsp_monster capture", "qc2cpp_test_qwsp_monster",
        lambda observation: observation.get("ready") is True
        and observation.get("nextthink", 0) > observation.get("server_time", 0)
        and (not player or observation.get("player", {}).get("ready") is True),
        timeout=timeout)
    require_live_qwsp_state(state, player=player)
    return state


def assert_qwsp_restore_map(snapshot):
    if snapshot.get("map") != "e1m2" or snapshot.get("player_model_index", 0) <= 0:
        raise ProcessFailure(f"QWSP restore did not load e1m2: {snapshot}")


def save_qwsp_state(process, basedir, name):
    # The test observer uses the same PR2/QCX save route as the server command,
    # but captures immediately after serializing in its one command-buffer pass.
    # A separate capture and save can cross an e1m2 monster think boundary,
    # making nextthink one scheduler tick older than the QCMS image.
    saved = process.observe(
        f"qc2cpp_test_qwsp_save {name}",
        "qc2cpp_test_qwsp_monster", timeout=8)
    require_live_qwsp_state(saved, player=True)
    assert_map_snapshot(
        process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12), "e1m2")
    save_path = basedir / "qw" / "save" / f"{name}.sav"
    if not save_path.is_file() or save_path.read_bytes()[:4] != b"QCMS":
        raise ProcessFailure(f"{name} did not produce a QCMS save")
    return saved, save_path


def advance_player(process, prior):
    player = player_signature(prior)
    process.observe(
        f'qc2cpp_test_stuff_client {player["slot"]} "+forward"',
        "qc2cpp_test_stuff_client", timeout=8)
    try:
        deadline = time.monotonic() + 12
        last = None
        while time.monotonic() < deadline:
            last = process.observe("qc2cpp_test_qwsp_monster read",
                "qc2cpp_test_qwsp_monster", timeout=min(1, deadline - time.monotonic()))
            if (last.get("player", {}).get("ready") is True
                    and sum(abs(last["player"]["origin"][axis] - player["origin"][axis])
                            for axis in range(3)) > 12.0):
                return last
            time.sleep(0.05)
        raise ProcessFailure(f"QWSP player did not move after +forward: {last}")
    finally:
        process.send(f'qc2cpp_test_stuff_client {player["slot"]} "-forward"')


def wait_for_player_processing(process, before, *, context):
    return wait_events(process,
        lambda events: events.get("client_prethink_count", 0)
        > before.get("client_prethink_count", 0)
        and events.get("client_postthink_count", 0)
        > before.get("client_postthink_count", 0),
        timeout=12, description=f"{context} did not resume player processing")


def restore_qwsp_state(process, name, expected, *, context):
    before = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8)
    process.send(f"load {name}")
    assert_map_snapshot(
        process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12), "e1m2")
    restored_events = wait_events(process,
        lambda events: events.get("restore_replication_begin_count", 0)
        == before.get("restore_replication_begin_count", 0) + 1
        and events.get("restore_replication_complete_count", 0)
        == before.get("restore_replication_complete_count", 0) + 1,
        timeout=12, description=f"{context} did not complete restore replication")
    for key in ("client_connect_count", "put_client_in_server_count"):
        if restored_events.get(key) != before.get(key):
            raise ProcessFailure(f"{context} replayed {key}: {before} -> {restored_events}")
    for key in ("init_count", "normal_unpublish_count"):
        if restored_events.get(key) != before.get(key, 0) + 1:
            raise ProcessFailure(f"{context} did not make exactly one fresh map session: "
                                 f"{before} -> {restored_events}")
    restored = capture_qwsp_state(process)
    assert_qwsp_restore_state(expected, restored, player=True, context=context)
    resumed = wait_qwsp_monster_progress(process, restored, timeout=12, player=True)
    require_live_qwsp_state(resumed, player=True)
    wait_for_player_processing(process, restored_events, context=context)
    return restored


def run_direct_suite(server, artifacts, assets, output, mode, client):
    output.mkdir(parents=True, exist_ok=True)
    run_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qwr-{mode[0]}-", dir="/tmp"))
    basedir = prepare_game_directory(run_root, assets, artifacts, mode)
    client_basedir = prepare_client_directory(run_root, assets)
    port = available_udp_port()
    process = RunningProcess(
        server_command(server, basedir, mode, port, start_map=False,
                       startup_command=("map", "e1m2")),
        run_root / "server.log")
    client_process = None
    try:
        assert_map_snapshot(
            process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12), "e1m2")
        alice_log = run_root / "alice.log"
        client_process = start_alice(client, client_basedir, port, alice_log)
        wait_for_client_events(process, client_process, alice_log,
            lambda events: events.get("client_connect_count") == 1
            and events.get("put_client_in_server_count") == 1,
            timeout=12, description="QWSP Alice did not become active")

        immediate, _ = save_qwsp_state(process, basedir, "qwsp-immediate")
        advanced = advance_player(process, immediate)
        if player_signature(advanced) == player_signature(immediate):
            raise ProcessFailure("QWSP player did not advance before immediate restore")
        advanced_monster = wait_qwsp_monster_progress(process, advanced, timeout=12,
                                                       player=True)
        require_live_qwsp_state(advanced_monster, player=True)
        restore_qwsp_state(process, "qwsp-immediate", immediate,
            context="immediate QWSP restore")

        death_saved, _ = save_qwsp_state(process, basedir, "qwsp-death")
        before_death = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8)
        player_slot = player_signature(death_saved)["slot"]
        process.observe(f'qc2cpp_test_stuff_client {player_slot} "kill"',
            "qc2cpp_test_stuff_client", timeout=8)
        wait_events(process,
            lambda events: events.get("normal_unpublish_count", 0)
            > before_death.get("normal_unpublish_count", 0),
            timeout=12, description="deathmatch 0 death did not restart e1m2")
        assert_map_snapshot(
            process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12), "e1m2")
        restore_qwsp_state(process, "qwsp-death", death_saved,
            context="death/restart QWSP restore")

        map_saved, _ = save_qwsp_state(process, basedir, "qwsp-map")
        before_map = process.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8)
        process.send("map e1m1")
        wait_events(process,
            lambda events: events.get("normal_unpublish_count", 0)
            > before_map.get("normal_unpublish_count", 0),
            timeout=12, description="map e1m1 did not replace e1m2")
        assert_map_snapshot(
            process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12), "e1m1")
        restore_qwsp_state(process, "qwsp-map", map_saved,
            context="map-change QWSP restore")
    except ProcessFailure as error:
        raise ProcessFailure(f"{error}; server log preserved in {run_root}") from error
    finally:
        close_client(client_process)
        process.close()


def run_cross_suite(server, source_artifacts, destination_artifacts, assets, output,
                    source_mode, destination_mode, client):
    output.mkdir(parents=True, exist_ok=True)
    source_root = pathlib.Path(tempfile.mkdtemp(prefix=f"qwr-{source_mode[0]}x-", dir="/tmp"))
    source_basedir = prepare_game_directory(source_root, assets, source_artifacts, source_mode)
    source_client_basedir = prepare_client_directory(source_root, assets)
    source_port = available_udp_port()
    source = RunningProcess(
        server_command(server, source_basedir, source_mode, source_port, start_map=False,
                       startup_command=("map", "e1m2")),
        source_root / "server.log")
    source_client = None
    try:
        assert_map_snapshot(
            source.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12), "e1m2")
        source_log = source_root / "alice.log"
        source_client = start_alice(client, source_client_basedir, source_port, source_log)
        wait_for_client_events(source, source_client, source_log,
            lambda events: events.get("client_connect_count") == 1
            and events.get("put_client_in_server_count") == 1,
            timeout=12, description="source QWSP Alice did not become active")
        expected, source_save = save_qwsp_state(source, source_basedir, "qwspcross")
        snapshot_bytes = source_save.read_bytes()
    except ProcessFailure as error:
        raise ProcessFailure(f"{error}; source server log preserved in {source_root}") from error
    finally:
        close_client(source_client)
        source.close()

    destination_root = pathlib.Path(tempfile.mkdtemp(
        prefix=f"qwr-{source_mode[0]}{destination_mode[0]}-", dir="/tmp"))
    destination_basedir = prepare_game_directory(
        destination_root, assets, destination_artifacts, destination_mode)
    destination_save = destination_basedir / "qw" / "save" / "qwspcross.sav"
    destination_save.parent.mkdir(parents=True)
    destination_save.write_bytes(snapshot_bytes)
    destination_client_basedir = prepare_client_directory(destination_root, assets)
    destination_port = available_udp_port()
    destination = RunningProcess(
        server_command(server, destination_basedir, destination_mode, destination_port,
                       start_map=False, startup_command=("load", "qwspcross")),
        destination_root / "server.log")
    destination_client = None
    try:
        assert_qwsp_restore_map(
            destination.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12))
        before_join = destination.observe("qc2cpp_test_events", "qc2cpp_test_events", timeout=8)
        destination_log = destination_root / "alice.log"
        destination_client = start_alice(
            client, destination_client_basedir, destination_port, destination_log)
        restored_events = wait_for_client_events(destination, destination_client, destination_log,
            lambda events: events.get("restore_replication_complete_count", 0)
            == before_join.get("restore_replication_complete_count", 0) + 1,
            timeout=12, description="cross-transport QWSP Alice did not restore")
        for key in ("client_connect_count", "put_client_in_server_count"):
            if restored_events.get(key) != before_join.get(key):
                raise ProcessFailure(
                    f"cross-transport QWSP restore replayed {key}: "
                    f"{before_join} -> {restored_events}")
        restored = capture_qwsp_state(destination)
        assert_qwsp_restore_state(expected, restored, player=True,
                                  context="cross-transport QWSP restore",
                                  player_identity=False)
        resumed = wait_qwsp_monster_progress(destination, restored, timeout=12, player=True)
        require_live_qwsp_state(resumed, player=True)
        wait_for_player_processing(destination, restored_events,
                                   context="cross-transport QWSP restore")
    except ProcessFailure as error:
        raise ProcessFailure(
            f"{error}; destination server log preserved in {destination_root}") from error
    finally:
        close_client(destination_client)
        destination.close()


def run_suite(server, source_artifacts, destination_artifacts, assets, output,
              source_mode, destination_mode, client):
    if source_mode == destination_mode:
        run_direct_suite(server, source_artifacts, assets, output, source_mode, client)
    else:
        run_cross_suite(server, source_artifacts, destination_artifacts, assets, output,
                        source_mode, destination_mode, client)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-mode", choices=("native", "wasm"), required=True)
    parser.add_argument("--destination-mode", choices=("native", "wasm"), required=True)
    parser.add_argument("--server", type=pathlib.Path, required=True)
    parser.add_argument("--source-artifacts", type=pathlib.Path, required=True)
    parser.add_argument("--destination-artifacts", type=pathlib.Path, required=True)
    parser.add_argument("--assets", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--client", type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        for required in (args.server, args.client):
            require_file(required)
        run_suite(args.server, args.source_artifacts, args.destination_artifacts,
                  args.assets, args.output, args.source_mode, args.destination_mode,
                  args.client)
    except ProcessFailure as error:
        print(f"QWSP restore acceptance: {error}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
