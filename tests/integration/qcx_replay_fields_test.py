import pathlib
import tempfile
import unittest

from qcx_replay_fields import write_fields


class FieldInventoryTests(unittest.TestCase):
    def check_inventory(self, declarations, expected):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            build = root / "qwsp-release-native"
            generated = root / "qwsp-release-generated"
            build.mkdir()
            (generated / "include/game").mkdir(parents=True)
            (generated / "runtime/include/game").mkdir(parents=True)
            (build / "CMakeCache.txt").write_text(
                f"CMAKE_HOME_DIRECTORY:INTERNAL={generated}\n")
            for filename, owner, field, shared, value_type in declarations:
                (generated / f"include/game/{filename}.hpp").write_text(
                    f"    {value_type} {field}{{}};\n"
                    f'qc::schema_member<&{owner}::{field}>("{field}", qc::SchemaUse::Save)\n')
                (generated / f"runtime/include/game/shared_{shared}_state.h").write_text("")
            destination = root / "fields.txt"
            write_fields(build / "game.dylib", destination)
            self.assertEqual(destination.read_text(), expected)

    def test_inventory_uses_the_retained_build_source_directory(self):
        self.check_inventory((
            ("globals", "Globals", "deathmatch", "global", "float"),
            ("entity_data", "EntityData", "health", "entity", "float")),
            "1 1 deathmatch\n2 1 health\n")

    def test_inventory_accepts_readable_aliases_with_the_same_wire_kinds(self):
        for string in ("String", "qc::String"):
            for vector in ("Vec3", "qc::Vec3"):
                with self.subTest(string=string, vector=vector):
                    self.check_inventory((
                        ("globals", "Globals", "mapname", "global", string),
                        ("entity_data", "EntityData", "origin", "entity", vector)),
                        "1 4 mapname\n2 3 origin\n")
        for mask in ("MaskFloat", "qc::MaskFloat", "qcx_mask_float_t"):
            with self.subTest(mask=mask):
                self.check_inventory((
                    ("globals", "Globals", "serverflags", "global", mask),
                    ("entity_data", "EntityData", "items", "entity", mask)),
                    "1 1 serverflags\n2 1 items\n")


if __name__ == "__main__":
    unittest.main()
