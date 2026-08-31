# qc2cpp adapter progress

This file records implementation progress for the approved canonical adapter
specification and its execution plan in qc2cpp.

## Tasks 8–10 — transport selection, canonical globals and entities

Implemented on branch `codex/mvdsv-qc2cpp-adapter`.

- The native transport resolves only `<fs_gamedir>/<sv_progsname>.<platform suffix>`
  with `RTLD_NOW | RTLD_LOCAL`, then obtains only `qc_game_plugin_query_v1`.
- Modes 4 (`qc2cpp-native`) and 5 (`qc2cpp-wasm`) enter the qc2cpp lifecycle
  before the legacy VM/PR1 path. A selected qc2cpp transport load failure is a
  server error; it does not fall through to PR1.
- Native-only builds use the installed C host SDK and have no Wasmtime link.
- Wasm builds additionally require `-DMVDSV_WASMTIME_ROOT=/path/to/wasmtime-sdk`;
  it provides the runtime library required by the optional SDK facade.
- A qc2cpp game publishes one ABI-validated shared globals block. Common QW
  scalar reads and writes use typed accessors into that block; the old VM
  globals remain only on the legacy/NQ path.
- `mapname` is set through the game semantic string API rather than the
  reserved legacy VM word. The view is cleared before the transport closes.
- The address returned by `game->init` is the validated entity-publication
  record. Its shared prefix, stride, capacity and address range are checked
  before `sv.edicts` are bound to owner slots; unpublish clears those views.
- qc2cpp-enabled builds use the SDK's declared shared entity type directly,
  while modes 0–3 retain their original `entvars_t` definition and
  `EDICT_TO_PROG` representation.
- Entity string reads and writes use named semantic operations with
  operation-local C buffers. Legacy string tokens are copied with an explicit
  NUL-inclusive capacity contract.
- Fixed entity callbacks are dispatched through their qc2cpp fixed entries;
  their shared words are presence markers, never PR1 function offsets.

The game host bindings are deliberately not complete yet. Tasks 10–13 add the
semantic engine services before a real generated game is expected to boot.
