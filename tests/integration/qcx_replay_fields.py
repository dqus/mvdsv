"""Diagnostic type inventory for the existing named QCGD codec, not a new ABI.

Only the retained generator's explicit schema/declaration forms are supported;
unknown fields fail closed rather than guessing their serialized widths.
"""
import pathlib
import re


def write_fields(game, destination):
    root = pathlib.Path(game).resolve().parents[1] / "qwsp-native-generated"
    rows = []
    for scope, filename, owner in ((1, "globals", "Globals"), (2, "entity_data", "EntityData")):
        generated = (root / f"include/game/{filename}.hpp").read_text()
        shared = (root / f"runtime/include/game/shared_{'global' if scope == 1 else 'entity'}_state.h").read_text()
        declarations = {}
        for text in (shared, generated):
            for line in text.splitlines():
                match = re.match(r"\s+(.*?)\s+(\w+)(?:\{.*|;)$", line)
                if match and not match[1].startswith("return"):
                    declarations[match[2]] = match[1]
        for field, name, use in re.findall(r'schema_member<&[^:>]+::(\w+)>\("([^"]+)", qc::SchemaUse::(\w+)\)', generated):
            if use == "Transient":
                continue
            kind = declarations.get(field, "")
            if kind in ("float", "qcx_mask_float_t"):
                code = 1
            elif kind in ("EntitySlot", "qcx_shared_entity_slot_t"):
                code = 2
            elif kind in ("qc::Vec3", "qc::SharedVec3", "qcx_vec3f_t"):
                code = 3
            elif kind == "qc::String":
                code = 4
            elif any(word in kind for word in ("Function<", "Callback<", "Field<")):
                code = 5
            else:
                raise ValueError(f"unsupported diagnostic field {owner}.{field}: {kind!r}")
            rows.append(f"{scope} {code} {name}\n")
    if not rows:
        raise ValueError("missing retained schemas")
    destination.write_text("".join(rows))
    return destination
