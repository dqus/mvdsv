#!/usr/bin/env python3
"""Unchanged id1 through QCX, with real client/physics and optional PR1 oracle."""
import argparse
import json
import pathlib
import shutil
import subprocess
import tempfile
import time

from qc2cpp_acceptance import (ProcessFailure, available_udp_port, prepare_game_directory,
    prepare_client_directory, server_command, wait_events, network_client_command,
    require_file, roster_client_command)
from qc2cpp_process import RunningProcess, run_client_network_acceptance
from qwsp_restore_acceptance import (close_client, wait_for_client_events,
    capture_qwsp_state, run_direct_suite, run_cross_suite, run_game_mode_suite)
from qwsp_monster_acceptance import wait_qwsp_monster_progress


def observe_gameplay(process, action="read", predicate=lambda state: state.get("ready")):
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        state = process.observe(f"qc2cpp_test_nq_gameplay {action}",
                                "qc2cpp_test_nq_gameplay", timeout=deadline - time.monotonic())
        if predicate(state):
            return state
        time.sleep(0.05)
    raise ProcessFailure(f"id1 gameplay {action} did not progress: {state}")


def run_reference(args, root):
    """Retained PR1 bytecode/server, not rebuilt from the candidate sources."""
    if args.reference_server is None:
        print("PR1 live comparison not configured; fixed PR1 characterization remains enabled")
        return
    require_file(args.reference_server)
    require_file(args.reference_progs)
    reference = root / "reference"
    basedir = prepare_client_directory(reference, args.assets)
    shutil.copy2(args.reference_progs, basedir / "qw" / "progs.dat")
    clientdir = prepare_client_directory(root / "reference-client", args.assets)
    port = available_udp_port()
    with (root / "reference-server.log").open("w") as output:
        server = subprocess.Popen([str(args.reference_server), "-basedir", str(basedir),
            "-game", "qw", "-port", str(port), "+sv_progtype", "0", "+sv_progsname", "progs",
            "+sv_forcenqprogs", "1", "+deathmatch", "0", "+map", "e1m2"], stdin=subprocess.PIPE,
            stdout=output, stderr=subprocess.STDOUT)
        try:
            run_client_network_acceptance(network_client_command(args.client, clientdir, port),
                                          root / "reference-client.log", timeout=25)
            if server.poll() is not None:
                raise ProcessFailure("PR1 NQ reference exited during client gameplay")
        finally:
            if server.poll() is None:
                server.stdin.write(b"quit\n")
                server.stdin.flush()
                try:
                    server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    server.terminate()
                    server.wait(timeout=3)
            server.stdin.close()
    text = (root / "reference-server.log").read_text(errors="replace")
    if "suicides" not in text or text.count(" entered the game") < 2:
        raise ProcessFailure(f"PR1 NQ reference did not suicide/restart/rejoin: {text}")


def run_gameplay(args):
    args.output.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(tempfile.mkdtemp(prefix="id1-game-", dir=args.output))
    run_reference(args, root)
    basedir = prepare_game_directory(root, args.assets, args.source_artifacts, args.source_mode)
    clientdir = prepare_client_directory(root, args.assets)
    port = available_udp_port()
    process = RunningProcess(server_command(args.server, basedir, args.source_mode, port,
        start_map=False) + ["+map", "e1m2"], root / "server.log")
    client = None
    try:
        process.observe_until("qc2cpp_test_snapshot", "qc2cpp_test_snapshot",
            lambda state: state["map"] == "e1m2" and state["frame_count"] > 3, timeout=20)
        client_log = root / "client.log"
        command = roster_client_command(args.client, clientdir, port,
            name="Alice", team="red", acceptance=False)
        with client_log.open("w") as log:
            client = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, text=True)
        wait_for_client_events(process, client, client_log,
            lambda events: events["put_client_in_server_count"] > 0,
            timeout=15, description="id1 Alice did not spawn")
        # A headless FTE window reports AFK (chat=2). MVDSV intentionally
        # filters attack buttons in that state; disable that client report,
        # not the server policy, before testing real weapon input.
        process.send('qc2cpp_test_stuff_client 0 "cl_sendchatstate 0"')
        observe_gameplay(process, predicate=lambda state: state["chat"] == 0)
        monster = capture_qwsp_state(process)
        wait_qwsp_monster_progress(process, monster, timeout=12, player=True)
        before = observe_gameplay(process, "prepare")
        process.send('qc2cpp_test_stuff_client 0 "+jump"')
        jumped = observe_gameplay(process, predicate=lambda state: state["jump_frames"] > 0)
        process.send('qc2cpp_test_stuff_client 0 "-jump"')
        observe_gameplay(process, "water")
        water = observe_gameplay(process, predicate=lambda state: state["water_frames"] > 0)
        observe_gameplay(process, "home")
        process.send('qc2cpp_test_stuff_client 0 "impulse 7"')
        process.send('qc2cpp_test_stuff_client 0 "+attack"')
        shot = observe_gameplay(process, predicate=lambda state: state["projectiles"] > 0
                               and state["muzzleflashes"] > 0)
        process.send('qc2cpp_test_stuff_client 0 "-attack"')
        compat = process.observe("qc2cpp_test_nq_compat", "qc2cpp_test_nq_compat", timeout=8)
        if (compat["profile"] != 2 or compat["mismatches"] != 0
                or compat["qw_missiles"] != 0
                or any(compat[key] == 0 for key in ("prethink", "postthink", "waterjump", "nq_missiles"))):
            raise ProcessFailure(f"id1 did not use consistent NQ engine behavior: {compat}")
        observe_gameplay(process, "exit")
        intermission = observe_gameplay(process,
            predicate=lambda state: state["intermission"] and state["intermissions"] > 0)
        time.sleep(2.1)  # Original id1 deliberately enforces a two-second exit delay.
        process.send('qc2cpp_test_stuff_client 0 "+attack"')
        changed = process.observe_until("qc2cpp_test_snapshot", "qc2cpp_test_snapshot",
            lambda state: state["map"] != "e1m2", timeout=15)
        if client.poll() is not None:
            raise ProcessFailure(f"FTE exited while decoding NQ intermission: {client_log.read_text()}")
        text = client_log.read_text(errors="replace")
        if any(error in text for error in ("Illegible server message", "Bad server message", "Host_Error")):
            raise ProcessFailure(f"FTE reported a protocol error: {text}")
        (root / "observations.json").write_text(json.dumps(dict(
            prepared=before, jumped=jumped, water=water, shot=shot, compatibility=compat,
            intermission=intermission, next_map=changed), indent=2))
        print(f"id1 {args.source_mode} gameplay evidence: {root}", flush=True)
    except ProcessFailure as error:
        raise ProcessFailure(f"{error}; id1 evidence preserved in {root}") from error
    finally:
        close_client(client)
        process.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", choices=("gameplay", "restore"), required=True)
    parser.add_argument("--source-mode", choices=("native", "wasm"), required=True)
    parser.add_argument("--destination-mode", choices=("native", "wasm"))
    for name in ("server", "source-artifacts", "destination-artifacts", "assets", "client",
                 "output", "reference-server", "reference-progs"):
        parser.add_argument(f"--{name}", type=pathlib.Path,
                            required=name in ("server", "source-artifacts", "assets", "client", "output"))
    args = parser.parse_args()
    if (args.reference_server is None) != (args.reference_progs is None):
        parser.error("reference server and progs must be supplied together")
    try:
        require_file(args.server)
        require_file(args.client)
        if args.suite == "gameplay":
            run_gameplay(args)
        elif args.source_mode == args.destination_mode:
            run_game_mode_suite(args.server, args.source_artifacts, args.assets,
                                args.output, args.source_mode)
            run_direct_suite(args.server, args.source_artifacts, args.assets,
                             args.output, args.source_mode, args.client, profile=2)
        else:
            run_cross_suite(args.server, args.source_artifacts, args.destination_artifacts,
                args.assets, args.output, args.source_mode, args.destination_mode, args.client)
    except ProcessFailure as error:
        print(f"NetQuake acceptance: {error}", flush=True)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
