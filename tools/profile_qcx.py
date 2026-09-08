#!/usr/bin/env python3
"""Capture a local MVDSV performance profile for one QW game mode."""

import argparse
import ctypes
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time


INTEGRATION_DIRECTORY = pathlib.Path(__file__).resolve().parents[1] / "tests" / "integration"
if str(INTEGRATION_DIRECTORY) not in sys.path:
    sys.path.insert(0, str(INTEGRATION_DIRECTORY))

from qc2cpp_acceptance import (  # noqa: E402
    available_udp_port,
    find_asset_file,
    link_input,
    prepare_client_directory,
)
from qc2cpp_process import ProcessFailure, RunningProcess  # noqa: E402


class RusageInfoV0(ctypes.Structure):
    _fields_ = [
        ("ri_uuid", ctypes.c_uint8 * 16),
        ("ri_user_time", ctypes.c_uint64),
        ("ri_system_time", ctypes.c_uint64),
        ("remainder", ctypes.c_uint64 * 8),
    ]


class MachTimebase(ctypes.Structure):
    _fields_ = [("numer", ctypes.c_uint32), ("denom", ctypes.c_uint32)]


def positive_int(value: str) -> int:
    result = int(value)
    if result <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return result


def positive_float(value: str) -> float:
    result = float(value)
    if result <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return result


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture a local PR1, QCX Native, or QCX Wasm server profile.")
    parser.add_argument("--mode", choices=("pr1", "native", "wasm"), required=True)
    parser.add_argument("--server", type=pathlib.Path, required=True,
                        help="MVDSV executable")
    parser.add_argument("--client", type=pathlib.Path, required=True,
                        help="FTEQW client executable")
    parser.add_argument("--assets", type=pathlib.Path, required=True,
                        help="read-only id1 asset directory")
    parser.add_argument("--game", type=pathlib.Path, required=True,
                        help="qwprogs.dat, game.dylib/game.so, or game.wasm")
    parser.add_argument("--output", type=pathlib.Path, required=True,
                        help="new directory that will receive profile artifacts")
    parser.add_argument("--map", default="povdmm4",
                        help="benchmark map (default: povdmm4)")
    parser.add_argument("--clients", type=positive_int, default=2,
                        help="number of headless FTE clients (default: 2)")
    parser.add_argument("--client-fps", type=positive_int, default=77,
                        help="headless client frame cap (default: 77)")
    parser.add_argument("--warmup", type=positive_float, default=5,
                        help="seconds after clients enter before measuring (default: 5)")
    parser.add_argument("--duration", type=positive_float, default=60,
                        help="seconds in each CPU window (default: 60)")
    parser.add_argument("--runs", type=positive_int, default=3,
                        help="number of independent server runs (default: 3)")
    parser.add_argument("--trace", action="store_true",
                        help="also capture Instruments Time Profiler via xctrace; "
                             "disable Record Waiting Threads in Instruments first")
    return parser.parse_args(argv)


def mode_program_type(mode: str) -> str:
    return {"pr1": "0", "native": "4", "wasm": "5"}[mode]


def game_destination(mode: str) -> pathlib.PurePath:
    if mode == "pr1":
        return pathlib.PurePath("qw/qwprogs.dat")
    if mode == "wasm":
        return pathlib.PurePath("qw/game.wasm")
    if mode == "native":
        if sys.platform == "darwin":
            return pathlib.PurePath("qw/game.dylib")
        if sys.platform.startswith("linux"):
            return pathlib.PurePath("qw/game.so")
        raise RuntimeError("native profiling is supported only on macOS and Linux")
    raise ValueError(f"unknown game mode: {mode}")


def server_command(arguments: argparse.Namespace, basedir: pathlib.Path,
                   port: int) -> list[str]:
    program_name = "qwprogs" if arguments.mode == "pr1" else "game"
    return [
        str(arguments.server.resolve()),
        "-basedir", str(basedir.resolve()),
        "-game", "qw",
        "-port", str(port),
        "+sv_progtype", mode_program_type(arguments.mode),
        "+sv_progsname", program_name,
        "+map", arguments.map,
    ]


def client_command(arguments: argparse.Namespace, basedir: pathlib.Path,
                   port: int, index: int) -> list[str]:
    return [
        str(arguments.client.resolve()),
        "-nosound",
        "-basedir", str(basedir.resolve()),
        "-game", "qw",
        "+vid_renderer", "headless",
        "+cl_maxfps", str(arguments.client_fps),
        "+name", f"QCXProfile{index}",
        "+connect", f"127.0.0.1:{port}",
    ]


def require_file(path: pathlib.Path, description: str) -> None:
    if not path.is_file():
        raise RuntimeError(f"{description} is unavailable: {path}")


def validate_arguments(arguments: argparse.Namespace) -> None:
    if sys.platform != "darwin":
        raise RuntimeError("QCX performance captures currently require macOS")
    require_file(arguments.server, "MVDSV executable")
    if not os.access(arguments.server, os.X_OK):
        raise RuntimeError(f"MVDSV executable is not executable: {arguments.server}")
    require_file(arguments.client, "FTEQW client executable")
    if not os.access(arguments.client, os.X_OK):
        raise RuntimeError(f"FTEQW client executable is not executable: {arguments.client}")
    if not arguments.assets.is_dir():
        raise RuntimeError(f"asset directory is unavailable: {arguments.assets}")
    require_file(arguments.assets / "maps" / f"{arguments.map}.bsp", "benchmark map")
    require_file(arguments.game, "game artifact")
    if arguments.output.exists():
        raise RuntimeError(f"output directory already exists: {arguments.output}")
    if arguments.trace and shutil.which("xcrun") is None:
        raise RuntimeError("--trace requires xcrun from Xcode")


def prepare_basedir(arguments: argparse.Namespace, run: pathlib.Path) -> pathlib.Path:
    if run.exists():
        raise RuntimeError(f"run directory already exists: {run}")
    basedir = run / "base"
    game_directory = basedir / "qw"
    game_directory.mkdir(parents=True)

    pak_found = False
    for pak_name in ("PAK0.PAK", "PAK1.PAK"):
        pak = find_asset_file(arguments.assets, pak_name)
        if pak is not None:
            link_input(pak, game_directory / pak_name.lower())
            pak_found = True
    if not pak_found:
        raise RuntimeError(f"asset directory has no PAK archives: {arguments.assets}")

    source_map = arguments.assets / "maps" / f"{arguments.map}.bsp"
    require_file(source_map, "benchmark map")
    destination_maps = game_directory / "maps"
    destination_maps.mkdir()
    link_input(source_map, destination_maps / source_map.name)
    source_entities = source_map.with_suffix(".ent")
    if source_entities.is_file():
        link_input(source_entities, destination_maps / source_entities.name)

    require_file(arguments.game, "game artifact")
    destination_game = basedir / game_destination(arguments.mode)
    destination_game.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(arguments.game, destination_game)
    return basedir


def mac_timebase() -> MachTimebase:
    if sys.platform != "darwin":
        raise RuntimeError("process CPU measurement currently requires macOS")
    system = ctypes.CDLL("/usr/lib/libSystem.B.dylib", use_errno=True)
    system.mach_timebase_info.argtypes = (ctypes.POINTER(MachTimebase),)
    system.mach_timebase_info.restype = ctypes.c_int
    timebase = MachTimebase()
    if system.mach_timebase_info(ctypes.byref(timebase)) != 0:
        raise OSError(ctypes.get_errno(), "mach_timebase_info")
    return timebase


def process_cpu_seconds(pid: int) -> float:
    if sys.platform != "darwin":
        raise RuntimeError("process CPU measurement currently requires macOS")
    process = ctypes.CDLL("/usr/lib/libproc.dylib", use_errno=True)
    process.proc_pid_rusage.argtypes = (ctypes.c_int, ctypes.c_int, ctypes.c_void_p)
    process.proc_pid_rusage.restype = ctypes.c_int
    usage = RusageInfoV0()
    if process.proc_pid_rusage(pid, 0, ctypes.byref(usage)) != 0:
        raise OSError(ctypes.get_errno(), "proc_pid_rusage")
    timebase = mac_timebase()
    ticks = usage.ri_user_time + usage.ri_system_time
    return ticks * timebase.numer / timebase.denom / 1_000_000_000


def trace_command(pid: int, trace: pathlib.Path, seconds: float) -> list[str]:
    return [
        "xcrun", "xctrace", "record",
        "--template", "Time Profiler",
        "--output", str(trace),
        "--time-limit", f"{seconds:g}s",
        "--attach", str(pid),
        "--no-prompt",
    ]


def terminate_process(process: subprocess.Popen) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def wait_for_clients(server: RunningProcess,
                     clients: list[tuple[subprocess.Popen, object]],
                     server_log: pathlib.Path, expected_clients: int) -> None:
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        server.drain_output()
        if not server.is_running:
            raise ProcessFailure("MVDSV exited before profile clients entered the game")
        for index, (client, _) in enumerate(clients):
            if client.poll() is not None:
                raise ProcessFailure(
                    f"profile client {index} exited before entering the game: "
                    f"{client.returncode}")
        if server_log.exists():
            contents = server_log.read_text(encoding="utf-8", errors="replace")
            if contents.count(" entered the game") >= expected_clients:
                return
        time.sleep(0.1)
    raise ProcessFailure(f"only some of {expected_clients} profile clients entered the game")


def measure_window(server: RunningProcess,
                   clients: list[tuple[subprocess.Popen, object]],
                   duration: float) -> dict[str, object]:
    if not server.is_running:
        raise ProcessFailure("MVDSV exited before CPU measurement")
    if any(client.poll() is not None for client, _ in clients):
        raise ProcessFailure("a profile client exited before CPU measurement")
    started_wall = time.monotonic()
    started_cpu = process_cpu_seconds(server.pid)
    time.sleep(duration)
    elapsed = time.monotonic() - started_wall
    ended_cpu = process_cpu_seconds(server.pid)
    server.drain_output()
    client_returncodes = [client.poll() for client, _ in clients]
    if not server.is_running or any(code is not None for code in client_returncodes):
        raise ProcessFailure(
            f"profile workload did not remain active: server_running={server.is_running}, "
            f"client_returncodes={client_returncodes}")
    cpu_seconds = ended_cpu - started_cpu
    return {
        "elapsed_seconds": elapsed,
        "process_cpu_seconds": cpu_seconds,
        "cpu_percent": cpu_seconds / elapsed * 100,
        "server_alive_after_window": True,
        "client_returncodes_after_window": client_returncodes,
    }


def run_once(arguments: argparse.Namespace, run: pathlib.Path) -> dict[str, object]:
    basedir = prepare_basedir(arguments, run)
    port = available_udp_port()
    server_log = run / "server.log"
    command = server_command(arguments, basedir, port)
    server = RunningProcess(command, server_log)
    clients: list[tuple[subprocess.Popen, object]] = []
    recorder = None
    trace_log = None
    try:
        time.sleep(0.5)
        client_commands = []
        for index in range(arguments.clients):
            client_basedir = prepare_client_directory(run / f"client-{index}",
                                                      arguments.assets)
            client_log = (run / f"client-{index}.log").open("w", encoding="utf-8")
            client_command_line = client_command(arguments, client_basedir, port, index)
            client = subprocess.Popen(
                client_command_line, cwd=run, stdout=client_log,
                stderr=subprocess.STDOUT)
            clients.append((client, client_log))
            client_commands.append(client_command_line)
        wait_for_clients(server, clients, server_log, arguments.clients)
        time.sleep(arguments.warmup)

        trace = None
        if arguments.trace:
            trace = run / "profile.trace"
            trace_log = (run / "xctrace.log").open("w", encoding="utf-8")
            recorder = subprocess.Popen(
                trace_command(server.pid, trace, arguments.duration),
                cwd=run, stdout=trace_log, stderr=subprocess.STDOUT)

        sample = measure_window(server, clients, arguments.duration)
        if recorder is not None:
            if recorder.wait(timeout=arguments.duration + 30) != 0:
                raise ProcessFailure("xctrace failed to capture the profile")
            export_command = [
                "xcrun", "xctrace", "export",
                "--input", str(trace),
                "--xpath", "/trace-toc/run[@number=\"1\"]/data/table[@schema=\"time-profile\"]",
                "--output", str(run / "profile.xml"),
            ]
            subprocess.run(export_command, cwd=run, stdout=trace_log,
                           stderr=subprocess.STDOUT, check=True)
            sample["trace"] = str(trace)
            sample["profile_xml"] = str(run / "profile.xml")

        sample["server_command"] = command
        sample["client_commands"] = client_commands
        sample["client_count"] = arguments.clients
        return sample
    finally:
        if recorder is not None:
            terminate_process(recorder)
        if trace_log is not None:
            trace_log.close()
        for client, client_log in clients:
            terminate_process(client)
            client_log.close()
        server.close()


def main(argv: list[str]) -> int:
    arguments = parse_arguments(argv)
    validate_arguments(arguments)
    arguments.output.mkdir(parents=True)
    timebase = mac_timebase()
    results = []
    for index in range(arguments.runs):
        result = run_once(arguments, arguments.output / f"run-{index + 1}")
        result["run"] = index + 1
        results.append(result)
        print(json.dumps(result, sort_keys=True), flush=True)
    summary = {
        "mode": arguments.mode,
        "map": arguments.map,
        "server": str(arguments.server.resolve()),
        "client": str(arguments.client.resolve()),
        "assets": str(arguments.assets.resolve()),
        "game": str(arguments.game.resolve()),
        "clients": arguments.clients,
        "client_fps": arguments.client_fps,
        "warmup_seconds": arguments.warmup,
        "duration_seconds": arguments.duration,
        "trace": arguments.trace,
        "timebase": {"numer": timebase.numer, "denom": timebase.denom},
        "runs": results,
    }
    (arguments.output / "summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
