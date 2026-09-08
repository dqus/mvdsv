#!/usr/bin/env python3
"""Unit tests for the local QCX performance profile runner."""

import importlib.util
import pathlib
import sys
import unittest


sys.dont_write_bytecode = True


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
PROFILE_TOOL = REPOSITORY_ROOT / "tools" / "profile_qcx.py"


def load_profile_tool():
    spec = importlib.util.spec_from_file_location("profile_qcx", PROFILE_TOOL)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load profile tool: {PROFILE_TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ProfileToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.profile = load_profile_tool()

    def parse(self, mode="wasm"):
        return self.profile.parse_arguments([
            "--mode", mode,
            "--server", "server",
            "--client", "fte",
            "--assets", "assets",
            "--game", "game",
            "--output", "output",
        ])

    def test_defaults_select_the_stable_povdmm4_workload(self):
        arguments = self.parse()
        self.assertEqual(arguments.map, "povdmm4")
        self.assertEqual(arguments.clients, 2)
        self.assertEqual(arguments.client_fps, 77)
        self.assertEqual(arguments.warmup, 5)
        self.assertEqual(arguments.duration, 60)
        self.assertEqual(arguments.runs, 3)
        self.assertFalse(arguments.trace)

    def test_modes_select_the_historical_program_type_and_game_name(self):
        self.assertEqual(self.profile.mode_program_type("pr1"), "0")
        self.assertEqual(self.profile.mode_program_type("native"), "4")
        self.assertEqual(self.profile.mode_program_type("wasm"), "5")
        self.assertEqual(self.profile.game_destination("pr1"),
                         pathlib.PurePath("qw/qwprogs.dat"))
        expected_native = "game.dylib" if sys.platform == "darwin" else "game.so"
        self.assertEqual(self.profile.game_destination("native").name, expected_native)
        self.assertEqual(self.profile.game_destination("wasm"),
                         pathlib.PurePath("qw/game.wasm"))

    def test_server_commands_select_the_mode_and_default_map(self):
        for mode, program_name in (("pr1", "qwprogs"), ("native", "game"),
                                   ("wasm", "game")):
            with self.subTest(mode=mode):
                command = self.profile.server_command(
                    self.parse(mode), pathlib.Path("base"), 27500)
                self.assertEqual(command[:5], [str(pathlib.Path("server").resolve()),
                                                "-basedir", str(pathlib.Path("base").resolve()),
                                                "-game", "qw"])
                self.assertEqual(command[command.index("+sv_progtype") + 1],
                                 self.profile.mode_program_type(mode))
                self.assertEqual(command[command.index("+sv_progsname") + 1],
                                 program_name)
                self.assertEqual(command[command.index("+map") + 1], "povdmm4")

    def test_headless_client_configuration_precedes_connect(self):
        command = self.profile.client_command(
            self.parse(), pathlib.Path("client-base"), 27500, 1)
        self.assertEqual(command[:2], [str(pathlib.Path("fte").resolve()), "-nosound"])
        self.assertEqual(command[command.index("+vid_renderer") + 1], "headless")
        self.assertEqual(command[command.index("+cl_maxfps") + 1], "77")
        self.assertLess(command.index("+vid_renderer"), command.index("+connect"))
        self.assertLess(command.index("+cl_maxfps"), command.index("+connect"))
        self.assertEqual(command[-2:], ["+connect", "127.0.0.1:27500"])


if __name__ == "__main__":
    unittest.main()
