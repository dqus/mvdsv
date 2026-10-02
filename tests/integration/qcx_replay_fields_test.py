import pathlib
import tempfile
import unittest

from qcx_replay_fields import write_fields


class FieldInventoryTests(unittest.TestCase):
    def test_inventory_uses_the_retained_build_source_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            build = root / "qwsp-release-native"
            generated = root / "qwsp-release-generated"
            build.mkdir()
            (generated / "include/game").mkdir(parents=True)
            (generated / "runtime/include/game").mkdir(parents=True)
            (build / "CMakeCache.txt").write_text(
                f"CMAKE_HOME_DIRECTORY:INTERNAL={generated}\n")
            for filename, owner, field, shared in (
                    ("globals", "Globals", "deathmatch", "global"),
                    ("entity_data", "EntityData", "health", "entity")):
                (generated / f"include/game/{filename}.hpp").write_text(
                    f"    float {field}{{}};\n"
                    f'qc::schema_member<&{owner}::{field}>("{field}", qc::SchemaUse::Save)\n')
                (generated / f"runtime/include/game/shared_{shared}_state.h").write_text("")
            destination = root / "fields.txt"
            write_fields(build / "game.dylib", destination)
            self.assertEqual(destination.read_text(), "1 1 deathmatch\n2 1 health\n")


if __name__ == "__main__":
    unittest.main()
