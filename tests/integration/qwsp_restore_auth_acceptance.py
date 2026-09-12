#!/usr/bin/env python3
"""Exercise restore authorization with controlled real QW UDP connections."""

import argparse
import pathlib
import re
import socket
import struct
import tempfile
import time
import unittest

from qc2cpp_acceptance import (
    available_udp_port,
    assert_map_snapshot,
    observe_restore_session,
    only_roster_client,
    prepare_game_directory,
    server_command,
    wait_restore_session,
)
from qc2cpp_process import ProcessFailure, RunningProcess


class QWConnection:
    """Keep direct-connect retries and pre-begin commands under test control."""

    def __init__(self, port):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.connect(("127.0.0.1", port))
        self.socket.settimeout(3)
        self.qport = self.socket.getsockname()[1]
        self.sequence = 1

    def close(self):
        self.socket.close()

    def connect(self, name, *, password, spectator):
        self.socket.send(b"\xff\xff\xff\xffgetchallenge\n")
        deadline = time.monotonic() + 3
        challenge = None
        while time.monotonic() < deadline:
            packet = self.socket.recv(65535)
            match = re.match(rb"\xff\xff\xff\xffc(-?\d+)", packet)
            if match:
                challenge = int(match[1])
                break
        if challenge is None:
            raise ProcessFailure("QW challenge response missing")
        info = (f"\\name\\{name}\\team\\blue\\password\\{password}"
                f"\\spectator\\{spectator}")
        self.socket.send((f'\xff\xff\xff\xffconnect 28 {self.qport} '
                          f'{challenge} "{info}"\n').encode("latin1"))
        while time.monotonic() < deadline:
            packet = self.socket.recv(65535)
            if packet.startswith(b"\xff\xff\xff\xff"):
                if packet[4:5] == b"j":
                    self.sequence = 1
                    return True
                if packet[4:5] == b"n":
                    return False
        raise ProcessFailure("QW admission response missing")

    def command(self, *commands):
        payload = b"".join(b"\x04" + command.encode("ascii") + b"\0"
                           for command in commands)
        self.socket.send(struct.pack("<IIH", self.sequence, 0, self.qport) + payload)
        self.sequence += 1


class RestoreAuthorization(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = pathlib.Path(tempfile.mkdtemp(prefix=f"qcx-auth-{cls.args.mode}-", dir="/tmp"))
        print(f"Restore authorization logs: {cls.root}", flush=True)
        cls.saves = {}
        cls.case_number = 0
        for saved_spectator in (False, True):
            root = cls.root / f"source-{int(saved_spectator)}"
            base = prepare_game_directory(root, cls.args.assets, cls.args.artifacts, cls.args.mode)
            port = available_udp_port()
            process = cls.start_server(base, port, root / "server.log")
            connection = QWConnection(port)
            try:
                assert_map_snapshot(process.observe("qc2cpp_test_snapshot",
                    "qc2cpp_test_snapshot", timeout=12), "e1m2")
                if not connection.connect("Saved", password="player-pass",
                                          spectator="spec-pass" if saved_spectator else "0"):
                    raise ProcessFailure("source admission failed")
                session = observe_restore_session(process)
                connection.command("new", f'spawn {session["spawncount"]} 0',
                                   f'begin {session["spawncount"]}')
                wait_restore_session(process,
                    lambda current: len(current["clients"]) == 1
                    and current["clients"][0]["state"] == 4,
                    timeout=8, description="source did not enter gameplay")
                process.send("save auth")
                process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=8)
                cls.saves[saved_spectator] = (base / "qw/save/auth.sav").read_bytes()
                if cls.saves[saved_spectator][:4] != b"QCMS":
                    raise ProcessFailure("source did not save QCMS")
            finally:
                connection.close()
                process.close()

    @classmethod
    def start_server(cls, base, port, log):
        command = server_command(cls.args.server, base, cls.args.mode, port, start_map=False)
        command += ["+sv_reconnectlimit", "0", "+qcx_restore_wait_timeout", "0",
                    "+map", "e1m2"]
        process = RunningProcess(command, log)
        process.send('password "player-pass"')
        process.send('spectator_password "spec-pass"')
        return process

    def fixture(self, saved_spectator):
        type(self).case_number += 1
        root = self.root / f"case-{self.case_number}"
        base = prepare_game_directory(root, self.args.assets, self.args.artifacts, self.args.mode)
        save = base / "qw/save/auth.sav"
        save.parent.mkdir()
        save.write_bytes(self.saves[saved_spectator])
        port = available_udp_port()
        self.process = self.start_server(base, port, root / "server.log")
        self.addCleanup(self.process.close)
        assert_map_snapshot(self.process.observe("qc2cpp_test_snapshot",
            "qc2cpp_test_snapshot", timeout=12), "e1m2")
        # "0" is a valid spectator password but requests a player connection.
        # This exercises saved-spectator admission independently of that role.
        if saved_spectator:
            self.process.send('spectator_password "0"')
        self.process.send("load auth")
        self.process.observe("qc2cpp_test_snapshot", "qc2cpp_test_snapshot", timeout=12)
        wait_restore_session(self.process,
            lambda session: session["waiting"] and session["available"] == 1,
            timeout=8, description="saved roster did not wait")
        self.connection = QWConnection(port)
        self.addCleanup(self.connection.close)

    def client(self, predicate=lambda client: True):
        session = wait_restore_session(self.process,
            lambda current: len(current["clients"]) == 1 and predicate(current["clients"][0]),
            timeout=5, description="client did not reach the expected state")
        return session, only_roster_client(session, "authorization connection")

    def saved_credentials(self, saved_spectator, *, both=False):
        return dict(password="player-pass" if both or not saved_spectator else "wrong-player",
                    spectator="0" if saved_spectator else "spec-pass" if both else "wrong-spec")

    def requested_credentials(self, saved_spectator):
        return dict(password="player-pass" if saved_spectator else "wrong-player",
                    spectator="" if saved_spectator else "spec-pass")

    def assert_requested_identity(self, client, saved_spectator):
        self.assertEqual(client["spectator"], not saved_spectator)
        self.assertEqual(client["team"], "blue")
        for key in ("userinfo_spectator", "wire_spectator"):
            self.assertEqual(client[key], "" if saved_spectator else "1")

    def begin(self, session):
        self.connection.command("new", f'spawn {session["spawncount"]} 0',
                                f'begin {session["spawncount"]}')

    def check_reconnect_saved_auth(self, saved_spectator):
        self.fixture(saved_spectator)
        credentials = self.saved_credentials(saved_spectator)
        self.assertTrue(self.connection.connect("Saved", **credentials))
        _, before = self.client(lambda client: client["pending"])
        self.assert_requested_identity(before, saved_spectator)
        self.assertTrue(self.connection.connect("Saved", **credentials),
                        "BOUND reconnect rejected valid saved-role credentials")
        session, after = self.client(lambda client: client["pending"] and client["userid"] != before["userid"])
        self.assert_requested_identity(after, saved_spectator)
        self.begin(session)
        _, active = self.client(lambda client: client["state"] == 4 and not client["pending"])
        self.assertEqual(active["spectator"], saved_spectator)

    def test_player_reconnect_uses_saved_role(self):
        self.check_reconnect_saved_auth(False)

    def test_spectator_reconnect_uses_saved_role(self):
        self.check_reconnect_saved_auth(True)

    def check_reconnect_denied_saved_auth(self, saved_spectator):
        self.fixture(saved_spectator)
        self.assertTrue(self.connection.connect("Saved", **self.saved_credentials(saved_spectator)))
        _, before = self.client(lambda client: client["pending"])
        self.assertFalse(self.connection.connect("Saved", **self.requested_credentials(saved_spectator)),
                         "BOUND reconnect accepted credentials for only the requested role")
        _, after = self.client(lambda client: client["pending"])
        self.assertEqual(after["userid"], before["userid"])
        self.assert_requested_identity(after, saved_spectator)

    def test_player_reconnect_rejects_requested_only_credentials(self):
        self.check_reconnect_denied_saved_auth(False)

    def test_spectator_reconnect_rejects_requested_only_credentials(self):
        self.check_reconnect_denied_saved_auth(True)

    def check_rename_denied_saved_auth(self, saved_spectator):
        self.fixture(saved_spectator)
        self.assertTrue(self.connection.connect("Unmatched", **self.requested_credentials(saved_spectator)))
        self.client(lambda client: client["waiting"])
        self.connection.command('setinfo name "Saved"')
        session, renamed = self.client(lambda client: client["name"] == "Saved")
        self.assertFalse(renamed["pending"], "rename bound a saved role without authorization")
        self.assertTrue(renamed["waiting"])
        self.assertEqual(session["available"], 1)
        self.begin(session)
        # A later packet must still be processed while gameplay remains blocked.
        self.connection.command('setinfo team "waiting"')
        _, waiting = self.client(lambda client: client["team"] == "waiting")
        self.assertNotEqual(waiting["state"], 4)
        self.assertTrue(waiting["waiting"])
        self.process.send("qcx_restore_continue")
        session, fallback = self.client(lambda client: not client["waiting"])
        self.begin(session)
        _, active = self.client(lambda client: client["state"] == 4)
        self.assertEqual(active["spectator"], not saved_spectator)
        self.assertEqual(active["team"], "waiting")

    def test_player_rename_requires_saved_authority_and_preserves_fallback(self):
        self.check_rename_denied_saved_auth(False)

    def test_spectator_rename_requires_saved_authority_and_preserves_fallback(self):
        self.check_rename_denied_saved_auth(True)

    def check_rename_with_saved_auth(self, saved_spectator):
        self.fixture(saved_spectator)
        self.assertTrue(self.connection.connect("Unmatched", **self.saved_credentials(saved_spectator, both=True)))
        self.client(lambda client: client["waiting"])
        self.connection.command('setinfo name "Saved"')
        session, bound = self.client(lambda client: client["pending"])
        self.assert_requested_identity(bound, saved_spectator)
        self.begin(session)
        _, active = self.client(lambda client: client["state"] == 4 and not client["pending"])
        self.assertEqual(active["spectator"], saved_spectator)

    def test_player_rename_accepts_saved_authority(self):
        self.check_rename_with_saved_auth(False)

    def test_spectator_rename_accepts_saved_authority(self):
        self.check_rename_with_saved_auth(True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("native", "wasm"), required=True)
    parser.add_argument("--server", type=pathlib.Path, required=True)
    parser.add_argument("--artifacts", type=pathlib.Path, required=True)
    parser.add_argument("--assets", type=pathlib.Path, required=True)
    RestoreAuthorization.args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(RestoreAuthorization)
    return 0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
