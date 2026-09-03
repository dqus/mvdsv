# qc2cpp adapter progress

This file records implementation progress for the approved canonical adapter
specification and its execution plan in qc2cpp.

## Tasks 8–12 — transport selection, canonical state, world and physics services

Implemented on branch `codex/mvdsv-qc2cpp-adapter`.

Task 11 passes a complete mandatory host table at plugin query time. World and
map imports are real MVDSV operations; imports owned by the following two tasks
fail closed until their explicit movement or network implementation replaces
them. The coverage ledger is `tests/qcx/host-imports.tsv`.

- The native transport resolves only `<fs_gamedir>/<sv_progsname>.<platform suffix>`
  with `RTLD_NOW | RTLD_LOCAL`, then obtains only `qcx_game_plugin_query_v1`.
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
- `tests/qcx/reentry_test.c` links the real `sv_phys.c` boundaries against a
  controlled qc2cpp game callback harness. It checks the two-sided impact
  sequence, live callback time, clamped think time and movement re-entry.
  `qc2cpp_entry_routes` checks the remaining direct source routes, and the
  optional `qc2cpp_reentry_fixture` transpiles plus builds the tracked QC
  fixture when `QC2CPP_COMPILER` is configured. The compact source of
  truth for those distinct proofs is `tests/qcx/entry-boundaries.tsv`.

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

`qcx_save_image_t` now owns a parsed `QCMS` V1 container independently of a
live server, transport or game instance. Its three fixed little-endian sections
are metadata, bounded engine state and the opaque logical guest payload. The
metadata identifies the logical game, map checksum and entity capacity; it does
not encode backend choice, addresses or a Wasmtime version. The engine section
explicitly records time, serverflags, lightstyles, precache order, edict
active/free/freetime data, and client slot flags/spawn parameters.

`QCX_SaveParse` accepts only the fixed section order and rejects truncation,
unknown versions, duplicate/trailing or overrun sections, invalid logical names
or capacities, non-finite timing, oversized resource lists and duplicate or
out-of-capacity client slots. It leaves its output null on every rejection.
`QCX_SaveEncode` writes the
same bounded, explicit representation into caller-owned `malloc` memory; it
does not serialize padded C structures. The same focused test target is built
and run under both native and Wasm adapter configurations. Server file I/O and
the atomic temporary-file/rename policy remain at the Task 18 integration seam,
where `QCX_SaveGame` selects a concrete save path.

## Boundary-correction Task 3 — QCX private adapter surface

The adapter implementation and its focused tests are now rooted at `src/qcx/`
and `tests/qcx/`. Private adapter names use the `QCX_` / `qcx_` spelling; the
product-facing configuration and acceptance-test names remain
`MVDSV_QC2CPP_*` and `qc2cpp_*` respectively. `QCX_ENABLED` is only emitted
for an enabled adapter build and configuration fails with
`QCX_ENABLED requires USE_PR2` if PR2 dispatch is disabled.

This consumes qc2cpp host-SDK commits `dd9f3e0` (public QCX ABI rename),
`fe17ae3` (topology-audit alignment), and `a08f9ac` (zero-base EntityData
publication). The native and Wasm focused adapter suites both cover the renamed
private targets; product acceptance coverage remains under its existing
`qc2cpp_*` names.

## Boundary-correction Task 4 — canonical globals and mapname

QCX now binds the published `qcx_shared_global_state_v1_t` directly as
`pr_global_struct` and `pr_globals`, after compile-time layout checks for the
shared QW globals. The prior binding is restored before the QCX publication is
cleared, so normal unload and non-terminal startup cleanup never leave a stale
game-owned pointer behind. Server code addresses standard globals directly with
`PR_GLOBAL`; no field-by-field `PR_Global_*` or `PR_SetGlobal_*` selection
facades remain.

`PR_SetMapName` is the only semantic mapname operation: it delegates to the
QCX string writer for a selected QCX game and otherwise uses the legacy generic
PR2 string path. The generic setter rejects an attempted legacy global-string
write while QCX is active instead of assuming every target is mapname.

## Boundary-correction Task 5 — PR2-owned gameplay dispatch

Gameplay sites in `sv_phys.c`, `sv_world.c` and `sv_user.c` now set canonical
globals and call the unchanged `PR_EdictTouch`, `PR_EdictThink` and
`PR_EdictBlocked` seams. They neither select QCX nor construct typed dispatcher
arguments. The existing PR2 implementations select the backend; when QCX is
active they ignore the scalar `func_t` argument, resolve `self`/`other` from
canonical globals and dispatch exactly once.

QCMS save/load selection likewise moved behind small PR2 operations. The
server retains its legacy save flow and owns subsequent prepared-restore
startup, while PR2 reports whether a selected QCX save was applied in place,
requires fresh startup, or failed. No callback registry or generic dispatch
mechanism was introduced. Focused source contracts and native/Wasm re-entry,
client, save and cross-transport restore acceptance tests cover this boundary.

## Boundary-correction Task 6 — backend-neutral server startup

`SV_SpawnServer` now owns only the common host-edict headers: it initializes
`e.entnum` and `e.area.ed` once for every selected program. The selected PR
boundary then binds the program-owned `edict.v`, globals and optional-field
view. QCX publishes its entity record during game initialization, but does not
bind host edicts or globals until that common host-header loop has completed.
It neither clears QCX entities at startup nor rewrites those headers.

The public server route uses `PR_*` aliases so a `USE_PR2=OFF` legacy build
keeps its original PR1 storage binding and rejects an impossible QCX restore.
PR2 owns the corresponding QCX/PR2 selection, including prepared-restore
resource preparation, validation and commit. The test-only real-server
observer requires every spawned QCX map to bind entity/global state before its
first gameplay entry and records every gameplay-capable host import made while
the game initializes; focused native and Wasm map tests run those checks.

## Boundary-correction Task 7 — slot-aware entity conversions

The selected PR boundary now has explicit encode, decode and entity-field
operations. A QCX entity reference is always a validated owner slot; legacy
PR1/PR2 VM execution retains its byte-offset representation. Live engine uses
of `owner`, `enemy`, `groundentity`, `dmg_inflictor`, `newmis`, and the two
`hideentity` consumers use that boundary rather than performing legacy stride
arithmetic. The checked source ledger confines the remaining conversion macros
to legacy VM execution or ABI definitions.

The real native and Wasm QW map tests write non-zero slots into shared entity
fields and confirm that the selected reverse operation yields the matching host
edict number. This exercises actual generated game memory without introducing
a second hand-written game ABI fixture.

## Boundary-correction Task 8 — fixed optional entity fields

Optional `fofs_*` values now have one fixed, validated QCX matrix at the PR2
startup boundary. Each row checks its exact name, type, physical layout,
non-zero in-stride offset and required host-access bits; an absent, duplicate or
invalid row remains unavailable (`0`). The resolver accepts extra known access
bits and exposes only the existing offsets, not descriptor lookup to callers.

Legacy PR1 and PR2 retain the existing `ED_FindFieldOffset` table in a small
legacy-only helper. `SV_SpawnServer` still does no optional-field discovery.

## Boundary-correction Task 9 — legacy string borrowing

`PR2_GetEntityString` is now the sole string-read boundary for QCX and uses a
private, read-through borrow table. A non-zero legacy token selects one of the
eleven string-field slots for its published entity; each borrow reads current
bytes from the game rather than caching a value. Token zero remains the empty
string, while an unreadable non-zero token is a server error.

Ordinary server call sites, including demo, visibility, player and antilag
paths, use `PR_GetEntityString` directly. The former string-specific
`QCX_CopyEntityString` branches were removed; PR1, legacy PR2 and the
NetQuake-only path retain their legacy resolvers.

## Tasks 18–20 — restore, terminal failure, and operations

`save`, `load`, and `saveload` now use the validated QCMS image in real MVDSV
flows. The acceptance matrix proves native/Wasm save combinations, connected
in-place restore, ordinary reconnection, and a valid post-commit restore that
exceeds the destination runtime. Invalid pre-commit input is recoverable;
post-commit failure is terminal.

Before the non-returning fatal boundary the adapter clears globals and entities
exactly once. The existing `sv_error` guard prevents guest shutdown, active
native unload, or executing Wasmtime-store destruction on that terminal path.
Normal map changes retain normal shutdown/reload behavior. Native and Wasm
subprocess tests exercise a real walkmove → touch nested fatal and require that
the outer QC callback cannot resume.

The maintained operator guide is [qc2cpp-adapter.md](qc2cpp-adapter.md). It
links the canonical qc2cpp spec and covers modes 4/5, dependencies, deployment,
QCMS compatibility, terminal behavior, acceptance, and Wasmtime updates.

## Boundary-correction Task 10 — focused cross-repository acceptance

The correction is accepted against qc2cpp
`a08f9ac8dee4e2eb75859b1e1552d0eddc3b925a` and MVDSV `d3c8856`, with the
Wasmtime 48.0.0 C API SDK. The focused qc2cpp matrix is green, including the
installed-SDK C consumer, native and real-Wasm host contracts, generated ID1
Wasm execution, and Draft34 code-generation topology. Both MVDSV configurations
rebuild from that installed SDK; all 24 private `qcx_*` tests pass in each.

The generated-game matrix passes native map/string/optional-field/fatal/restore,
FTE client/network/spectator and connected-save coverage, together with native
to native, Wasm to Wasm, native to Wasm and Wasm to native QCMS saves. The map
tests are also the real generated-game proof for non-zero entity references;
there is no redundant standalone process target.

During the final source audit, obsolete `QCX_Active()` guards in `sv_user.c`
were found after canonical globals had already been bound. They suppressed the
required `self`, `time`, `frametime` and parameter writes immediately before
PR2 entries. Corrective commit `d3c8856` removes those guards and the now-unused
private headers from ordinary server files. A focused source contract and
native/Wasm route, re-entry, map, network and spectator tests prove selection
remains exclusively in PR2. The only remaining `sv_*` private include is the
`QCX_TESTS`-guarded observer registration in `sv_main.c`; it is test-only,
contains no backend selection, and is not compiled into production binaries.

The final audits leave only self-auditing obsolete-name literals in
`tests/qcx/naming_test.cmake`; no old public ABI spelling, `PR_Global_*` facade,
or QCX dispatch/selection remains in ordinary server code. The checked
entity-reference ledger continues to classify every legacy conversion hit as a
legacy-only VM path or selected PR boundary. Draft34 remains owner-local: the
correction did not recreate `game_declarations.hpp`, a central declaration
aggregate, or giant generated source files.

## Canonical entity conversions

`EDICT_TO_PROG` and `PROG_TO_EDICT` are now real functions in `pr_edict.c`.
They preserve legacy byte offsets when QCX is inactive and select validated
QCX slots when it is active. `PR_EntityFieldToEdict` lives beside them and
delegates to `PROG_TO_EDICT`; the parallel `PR*_EntityReference` facade and
`pr_entity_references.c` were removed.

The PR1 and PR2 non-QCX server builds, focused legacy/QCX unit tests, source
route audit, and real native/Wasm server-map acceptance all pass.

## Post-Task 10 — optional-field discovery acceptance

Commit `21abe39` centralizes the fixed optional-field matrix in `pr_edict.c`:
the same table resets all `fofs_*` values and preserves legacy
`ED_FindFieldOffset` discovery. This is a post-plan cleanup, not a retroactive
change to the executed boundary-correction plan.

The focused optional-field evidence was rerun against qc2cpp
`0063ac8cb6b3b3fae6c0068c0f3eac540eb7686f` and MVDSV
`54be5fb42276d762218fb60710f2b5e8f1969465` (which contains `21abe39`). Both
native and Wasm configurations passed `qcx_optional_fields`,
`qcx_optional_field_routes`, `qcx_legacy_optional_fields_pr1`,
`qcx_legacy_optional_fields_pr2`, and `qc2cpp_server_map_native` or
`qc2cpp_server_map_wasm` respectively. This records the focused follow-up
evidence; it does not replace the full Task 10 proof matrix above.
