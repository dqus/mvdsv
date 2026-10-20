#!/usr/bin/env python3
"""Real QCX cvar lifetime across ordinary map startup and save/load."""
import argparse
import pathlib
import tempfile

from qc2cpp_acceptance import available_udp_port, prepare_game_directory, server_command
from qc2cpp_process import ProcessFailure, RunningProcess


def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    # Keep legacy bounded server save paths short. Assets remain read-only.
    root = pathlib.Path(tempfile.mkdtemp(prefix="qcx-reg-"))
    log = args.output / f"registercvar-{args.mode}-{root.name}.log"
    basedir = prepare_game_directory(root, args.assets, args.artifacts, args.mode,
                                     entity_override=True)
    (basedir / "qw/maps/e1m2.ent").write_text('{"classname" "worldspawn"}\n')
    command = server_command(args.server, basedir, args.mode, available_udp_port(),
                             start_map=False)
    command += ["+set", "qcx_reg_existing", "41", "+set", "qcx_reg_request", "0",
                "+set", "qcx_reg_action", "0", "+map", "e1m2"]
    process = RunningProcess(command, log)
    sequence = 0

    def snapshot():
        return process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)

    def probe(action=0):
        nonlocal sequence
        sequence += 1
        process.send(f"qcx_reg_action {action}")
        state = process.observe_until(f"qcx_reg_request {sequence}", "registercvar_probe",
            lambda state: state["request"] == sequence, timeout=12)
        expected = {"request": sequence, "pass": 1}
        if state != expected:
            raise ProcessFailure(f"registercvar mismatch: {state} != {expected}; log: {log}")
        # Query the console's real cvar registry too, not only guest output.
        process.send("qcx_reg_probe")
        process.send("qcx_reg_existing")
        snapshot()

    try:
        snapshot()
        probe()
        probe(action=1)
        process.send("qcx_reg_action 0")
        process.send("qcx_reg_request 0")
        process.send("map e1m2")
        snapshot()
        probe()
        process.send("save registercvar")
        snapshot()
        save = basedir / "qw/save/registercvar.sav"
        if not save.is_file() or save.read_bytes()[:4] != b"QCMS":
            raise ProcessFailure(f"missing QCMS save: {save}; log: {log}")
        probe(action=2)
        process.send("qcx_reg_action 0")
        process.send("qcx_reg_request 0")
        process.send("load registercvar")
        snapshot()
        probe()
    finally:
        process.close()
    text = log.read_text(errors="replace")
    for value in ("7.25", "17", "23"):
        if f'"qcx_reg_probe" is "{value}"' not in text:
            raise ProcessFailure(f"engine console did not confirm value {value}; log: {log}")
    if '"qcx_reg_existing" is "41"' not in text:
        raise ProcessFailure(f"engine console did not preserve configured value; log: {log}")
    print(f"registercvar {args.mode}: create/preserve/map/save-load passed; log: {log}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("server", "assets", "artifacts", "output"):
        parser.add_argument(f"--{name}", type=pathlib.Path, required=True)
    parser.add_argument("--mode", choices=("native", "wasm"), required=True)
    run(parser.parse_args())
