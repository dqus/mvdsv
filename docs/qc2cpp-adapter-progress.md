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

## Tasks 13–14 — QW bootstrap and real map-lifecycle acceptance

The adapter now boots the transpiled QW corpus through both real transports.
The optional `qc2cpp_acceptance_assets` target generates the QW project,
builds the requested native library or fixed-memory Wasm module, and runs the
generated-project checker before acceptance starts. It is deliberately opt-in:
the ordinary MVDSV build never needs local game assets, qc2cpp, or a WASI SDK.

- The generated QW runtime has 2,048 owner slots and a 512 KiB string arena;
  this accommodates the real `e1m2` entity set and its map strings.
- The generated Wasm game and the Wasmtime host require the same fixed,
  non-shared wasm32 128/128-page (8 MiB) profile. It is still not growable.
- `qc2cpp_server_map_native` and `qc2cpp_server_map_wasm` start a real MVDSV
  server on `e1m1`, execute `map e1m2`, and require an exact owner-local
  observer result: both maps reach frames, one normal unpublish occurs, and no
  legacy gameplay entry or gameplay during initialization occurs.
- The observer exists only in `MVDSV_QC2CPP_TESTS` builds. It records facts
  without changing game state; production server binaries expose none of its
  commands.
- The process runner gives each execution a fresh temporary game directory,
  reads timestamp-prefixed JSON observations with bounded waits, preserves a
  failed server's combined log, and terminates it deterministically.
- The acceptance generation target tracks every runtime, game ABI, checker and
  Wasm-toolchain resource copied by qc2cpp, not just the compiler and QC
  sources. A resource update therefore regenerates both transport projects
  before their checker and server-lifecycle proofs run.

## Task 15 — bounded real FTE client observation

The optional `QC2CPP_FTE_CLIENT` cache entry registers
`qc2cpp_client_native`. It gives a real FTE client and the server separate
temporary QW directories, supplies only local PAK assets by symlink, and runs
the generated native game on a private UDP port. The runner accepts only these
ordered observations from the explicitly enabled client:

1. `headless-renderer` — FTE selected its existing null renderer before startup;
2. `parsed-qw-server` — a real QW server message was parsed; and
3. `received-qw-frame` — a subsequent client frame ran.

The client exits after that bounded evidence. A normal successful run therefore
proves actual packet parsing but intentionally does not claim spawn, movement,
or active-play state; Task 16 owns those server-authoritative assertions. An
unreachable port times out and fails, rather than being treated as a successful
connection.

The FTE source basis is upstream `f937b9d88f71fc4429db5fe56c6a98d922711b2e`;
the explicitly enabled acceptance instrumentation is
`b16545bffb7a06c6437a1b04ffe900f0dd3489fa`. Its existing Q1QVM server audit
files retain their pinned digests. During the first real run, generated QW called
logical print, info-key, numeric conversion,
multicast and frag-log services that had typed ABI imports but no
`TargetServiceHooks` binding. qc2cpp now binds those existing imports; its
generated-game contract invokes every such service, so a future missing binding
fails closed rather than becoming a harmless-looking client test pass.

## Task 16 — real QW gameplay, reconnect and spectator acceptance

`qc2cpp_network_native` and `qc2cpp_network_wasm` keep a real FTE client
connected through QW signon and active play. The enabled client drives normal
forward input, `kill`, and disconnect; the server-side observer verifies the
resulting guest callbacks and host-owned outcomes rather than treating a
handshake as gameplay. Each network test performs two ordinary player sessions,
changes from `e1m1` to `e1m2`, then completes a third session. It requires:

- successful signon and active client state;
- server-observed player movement, the real teledeath touch owner/toucher pair,
  the QW `ClientKill`/`respawn` result (live player state and the stock
  `-2` suicide-frag transition), and client disconnect callbacks;
- no legacy gameplay entry, correct shared `self` during player allocation,
  and a normal qc2cpp unpublish over the map change.

`qc2cpp_spectator_native` and `qc2cpp_spectator_wasm` separately connect with
the real QW `spectator` userinfo, then require the spectator spawn and
`spectator_think` guest path before disconnecting. The test observer remains
test-build-only and only records these facts.

Each transport-specific acceptance-assets target depends on `mvdsv`. This
prevents both the aggregate and direct native/Wasm target paths from checking a
new generated game against an old server binary after an adapter source edit.

## Task 17 — validated transport-independent save image

`qc_save_image_t` now owns a parsed `QCMS` V1 container independently of a
live server, transport or game instance. Its three fixed little-endian sections
are metadata, bounded engine state and the opaque logical guest payload. The
metadata identifies the logical game, map checksum and entity capacity; it does
not encode backend choice, addresses or a Wasmtime version. The engine section
explicitly records time, serverflags, lightstyles, precache order, edict
active/free/freetime data, and client slot flags/spawn parameters.

`QC_SaveParse` accepts only the fixed section order and rejects truncation,
unknown versions, duplicate/trailing or overrun sections, invalid logical names
or capacities, non-finite timing, oversized resource lists and duplicate or
out-of-capacity client slots. It leaves its output null on every rejection.
`QC_SaveEncode` writes the
same bounded, explicit representation into caller-owned `malloc` memory; it
does not serialize padded C structures. The same focused test target is built
and run under both native and Wasm adapter configurations. Server file I/O and
the atomic temporary-file/rename policy remain at the Task 18 integration seam,
where `QC_SaveGame` selects a concrete save path.
