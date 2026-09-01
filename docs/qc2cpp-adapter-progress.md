# qc2cpp adapter progress

This file records implementation progress for the approved canonical adapter
specification and its execution plan in qc2cpp.

## Tasks 8–12 — transport selection, canonical state, world and physics services

Implemented on branch `codex/mvdsv-qc2cpp-adapter`.

Task 11 passes a complete mandatory host table at plugin query time. World and
map imports are real MVDSV operations; imports owned by the following two tasks
fail closed until their explicit movement or network implementation replaces
them. The coverage ledger is `tests/qc2cpp/host-imports.tsv`.

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

Task 12 completes the physics-side host imports and routes the engine's exact
touch/think/blocked boundaries directly to the typed qc2cpp entries. The
slot-level transport functions remain the ABI boundary below these engine
entries; no callback is reconstructed from a legacy function offset.

- `traceline`, `checkclient`, `walkmove`, `droptofloor`, `checkbottom`,
  `pointcontents`, `aim` and `step_direction` now invoke normal MVDSV engine
  operations. A trace result contains a canonical owner slot, never a legacy
  edict byte offset.
- `walkmove` restores only the caller's canonical `self` after nested work.
  It intentionally leaves `time`, `other` and ordinary globals live.
- `SV_Impact`, `SV_RunThink`, pusher callbacks, trigger links and user-physics
  callbacks pass their actual entities and current time to the engine entries.
  `SV_Impact` retains one initial time write and its legacy `self`/`other`
  restoration boundary; it does not roll back callback-produced time.
- `tests/qc2cpp/reentry_test.c` links the real `sv_phys.c` boundaries against a
  controlled qc2cpp game callback harness. It checks the two-sided impact
  sequence, live callback time, clamped think time and movement re-entry.
  `qc2cpp_entry_routes` checks the remaining direct source routes, and the
  optional `qc2cpp_reentry_fixture` transpiles plus builds the tracked QC
  fixture when `QC2CPP_COMPILER` is configured. The compact source of
  truth for those distinct proofs is `tests/qc2cpp/entry-boundaries.tsv`.

Only network/client host services remain before a real generated game is
expected to boot; Task 13 owns those explicit bindings.
