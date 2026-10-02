#!/usr/bin/env python3
"""Real UDP capture smoke; no workload/CPU equivalence claim until replay exists."""
import argparse
import binascii
import pathlib
import re
import struct
import subprocess
import json
import sys
import tempfile
import time

SOURCE = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SOURCE / "tools"))
from profile_qcx import prepare_basedir, server_command, available_udp_port
from qc2cpp_process import RunningProcess
from qwsp_restore_auth_acceptance import QWConnection
from qcx_replay_fields import write_fields


def inspect_capture(path):
    """Check real hooks, including recovery and absence of recursive records."""
    data = path.read_bytes()
    offset = 20
    for _ in range(3):
        size = struct.unpack_from("<I", data, offset)[0]
        offset += 4 + size
    begin, end, count = struct.unpack_from("<QQQ", data, offset)
    offset += 24
    kinds, moves, bootstrap = [], [], []
    for _ in range(count):
        kind, slot = struct.unpack_from("<II", data, offset)
        offset += 48
        kinds.append(kind)
        if kind in (7, 8):
            command = [struct.unpack_from("<Bfh", data, offset + 7*i) for i in range(3)]
            offset += 21
            if kind == 7:
                moves.append(command)
        size = struct.unpack_from("<I", data, offset)[0]
        offset += 4 + size
        if kind in (2, 3, 4):
            bootstrap.append((kind, slot))
    if offset != len(data) or not (0 < begin < end < count):
        raise RuntimeError("invalid captured boundaries")
    if kinds.count(6) != 24 or kinds.count(8) != 24 or len(moves) != 28:
        raise RuntimeError("actual move/recovery/recursive boundaries differ")
    if sum(cmd[0][0] == 100 for cmd in moves) != 2 or sum(cmd[1][0] & 1 for cmd in moves) != 24:
        raise RuntimeError("missing long commands or firing input")
    if sorted(bootstrap) != [(kind, slot) for kind in (2, 3, 4) for slot in (0, 1)]:
        raise RuntimeError("incomplete real bootstrap")
    return {"events": count, "groups": 24, "commands": len(moves), "long_commands": 2}


def move(connection, *, msec=13, dropped=0):
    # The protocol's fixed checksum table belongs to the audited engine source.
    text = (SOURCE / "src/common.c").read_text()
    table_text = text.split("static byte chktbl[1024] = {", 1)[1].split("};", 1)[0]
    table = bytes(int(word, 16) for word in re.findall(r"0x[0-9a-fA-F]+", table_text))
    table += bytes(1024 - len(table))
    connection.sequence += dropped
    sequence = connection.sequence
    # Three delta commands; the newest has forward movement + attack.
    body = b"\0" + b"\0\x0d" + b"\0\x0d" + b"\x24" + struct.pack("<hBB", 400, 1, msec)
    p = table[sequence % 1020:sequence % 1020 + 4]
    salt = bytes(((sequence & 255) ^ p[0], p[1], ((sequence >> 8) & 255) ^ p[2], p[3]))
    crc = binascii.crc_hqx(body[:60] + salt, 0xffff) & 255
    packet = struct.pack("<IIH", sequence, 0, connection.qport) + b"\x03" + bytes([crc]) + body
    connection.socket.send(packet)
    connection.sequence += 1


def capture(args):
    root = pathlib.Path(tempfile.mkdtemp(prefix="qcx-cap-", dir="/tmp"))
    args.mode, args.map = "native", "povdmm4"
    base = prepare_basedir(args, root / "capture")
    port = available_udp_port()
    args.output.mkdir(parents=True, exist_ok=True)
    tape = args.output.resolve() / "capture.tape"
    fields = write_fields(args.game, args.output.resolve() / "fields.txt")
    if tape.exists():
        raise RuntimeError(f"refusing to overwrite {tape}")
    command = server_command(args, base, port)[:-2] + [
        "-qcx-probe-record", str(tape), "-qcx-probe-fields", str(fields), "+deathmatch", "4", "+sv_speedcheck", "0",
        "+sv_antilag", "0", "+sv_minping", "0", "+sv_loadentfiles", "1", "+sv_getrealip", "0",
        "+sv_hashpasswords", "0", "+password", "probepass", "+map", "povdmm4"]
    if getattr(args, "reject_world_key", None):
        command[-2:-2] = ["+localinfo", args.reject_world_key, "1"]
    if getattr(args, "detail", False):
        command.insert(1, "-qcx-probe-detail")
    process = RunningProcess(command, args.output / "capture.log")
    clients = []
    try:
        if getattr(args, "reject_world_key", None):
            process.wait_for_exit(timeout=8)
            log = (args.output / "capture.log").read_text()
            if tape.exists() or "unsupported serverinfo key" not in log:
                raise RuntimeError("unsupported effective world info was not rejected")
            print(f"capture rejected localinfo {args.reject_world_key}; no complete tape")
            return None
        status = process.observe("qcx_probe_status", "qcx_probe_status", timeout=12)
        for i in range(2):
            connection = QWConnection(port)
            clients.append(connection)
            if not connection.connect(f"Probe{i}", password="probepass", spectator="0"):
                raise RuntimeError(f"test client admission failed: {connection.last_print}")
        # Interleaved phases, not one synthetic join per client.
        for connection in clients:
            # Last status chunk avoids requiring a renderer/download client;
            # ordinary Cmd_Spawn_f still performs the actual edict setup.
            connection.command("new", "pext", "new", f'spawn {status["spawncount"]} 31')
        for connection in clients:
            connection.command(f'begin {status["spawncount"]}')
        process.observe_until("qcx_probe_status", "qcx_probe_status",
                              lambda s: len(s["spawned"]) == 2, timeout=8)
        for i in range(12):
            for connection in clients:
                move(connection, msec=100 if i == 3 else 13, dropped=2 if i == 5 else 0)
            time.sleep(.02)
        if getattr(args, "reject_command", None):
            clients[0].command(args.reject_command)
            code = process.wait_for_exit(timeout=8)
            log = (args.output / "capture.log").read_text()
            if code == 0 or tape.exists() or "uncaptured client string command" not in log:
                raise RuntimeError("unsupported gameplay command was not rejected")
            print(f"capture rejected unsupported {args.reject_command}; no complete tape")
            return None
        result = process.observe("qcx_probe_finish", "qcx_probe_finished", timeout=8)
        if not tape.is_file() or tape.stat().st_size == 0:
            raise RuntimeError("capture did not create a tape")
        print(f"capture boundary smoke: {result}; work={inspect_capture(tape)}; tape={tape}; staging={root}")
        return tape
    finally:
        for connection in clients:
            connection.close()
        process.close()


def event_offsets(data):
    offset = 20
    for _ in range(3):
        size = struct.unpack_from("<I", data, offset)[0]
        offset += 4 + size
    count = struct.unpack_from("<Q", data, offset + 16)[0]
    offset += 24
    for _ in range(count):
        kind = struct.unpack_from("<I", data, offset)[0]
        yield kind, offset
        offset += 48 + (21 if kind in (7, 8) else 0)
        size = struct.unpack_from("<I", data, offset)[0]
        offset += 4 + size


def replay(args, tape, label, expected):
    root = pathlib.Path(tempfile.mkdtemp(prefix="qcx-val-", dir="/tmp"))
    base = prepare_basedir(args, root / "run")
    command = server_command(args, base, available_udp_port())[:-2] + [
        "-qcx-probe-replay", str(tape), "-qcx-probe-fields", str(args.output.resolve() / "fields.txt")]
    result = subprocess.run(command, capture_output=True, text=True, timeout=12)
    log = result.stdout + result.stderr
    (args.output / f"{label}.log").write_text(log)
    rows = [json.loads(line[line.index('{'):]) for line in log.splitlines()
            if '{"qcx_probe_execution"' in line]
    ok = result.returncode == 0 and bool(rows) and rows[-1]["qcx_probe_execution"]["ok"]
    if ok != expected:
        raise RuntimeError(f"{label}: unexpected validation result, see log")
    if not expected and "diverged" not in log and "valid tape" not in log:
        raise RuntimeError(f"{label}: failed without input/state validation evidence")
    return rows[-1] if rows else {"rejected": True}


def integration(args):
    tape = capture(args)
    if tape is None:
        return
    print("capture-to-replay:", replay(args, tape, "replay", True))
    data = bytearray(tape.read_bytes())
    for kind, offset in event_offsets(data):
        if kind == 7:
            # Change accepted movement only. Order/clocks/checkpoints remain intact.
            struct.pack_into("<h", data, offset + 53, -400)
    changed = args.output.resolve() / "changed.tape"
    changed.write_bytes(data)
    print("changed command:", replay(args, changed, "changed", False))
    truncated = args.output.resolve() / "truncated.tape"
    truncated.write_bytes(tape.read_bytes()[:-7])
    print("truncated input:", replay(args, truncated, "truncated", False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("server", "game", "assets", "output"):
        parser.add_argument(f"--{name}", type=pathlib.Path, required=True)
    parser.add_argument("--reject-command", choices=("kill", "observe", "airstep"))
    parser.add_argument("--reject-world-key", choices=("axe", "dq", "dr"))
    parser.add_argument("--detail", action="store_true")
    integration(parser.parse_args())
