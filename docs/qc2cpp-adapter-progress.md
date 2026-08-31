# qc2cpp adapter progress

This file records implementation progress for the approved canonical adapter
specification and its execution plan in qc2cpp.

## Task 8 — transport selection and loading

In progress on branch `codex/mvdsv-qc2cpp-adapter`.

- The native transport resolves only `<fs_gamedir>/<sv_progsname>.<platform suffix>`
  with `RTLD_NOW | RTLD_LOCAL`, then obtains only `qc_game_plugin_query_v1`.
- Modes 4 (`qc2cpp-native`) and 5 (`qc2cpp-wasm`) enter the qc2cpp lifecycle
  before the legacy VM/PR1 path. A selected qc2cpp transport load failure is a
  server error; it does not fall through to PR1.
- Native-only builds use the installed C host SDK and have no Wasmtime link.
- Wasm builds additionally require `-DMVDSV_WASMTIME_ROOT=/path/to/wasmtime-sdk`;
  it provides the runtime library required by the optional SDK facade.

The game host bindings are deliberately not complete yet. Tasks 9–13 add the
semantic engine services before a real generated game is expected to boot.
