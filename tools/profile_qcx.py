#!/usr/bin/env python3
"""Capture a local MVDSV performance profile for one QW game mode."""

import argparse
import pathlib
import sys


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
