# PR2-Backed QCX Service Design

## Goal

Use the existing PR2 builtin implementation as the shared server-side implementation for QCX host services where PR2 and QCX have the same semantics, instead of routing QCX through PR1-owned `SV_QC_*` helpers or duplicating PR2 logic inside `src/qcx/services_*.c`.

## Architectural boundary

PR2 remains the mandatory dispatch layer for QCX. QCX already requires `USE_PR2`, `sv_progtype` already contains the QCX native/Wasm program types, and PR2 already has QCX-aware entity/string/global routing.

The desired call shape is:

```text
PR2 VM syscall                 QCX host import
     |                              |
     | decode VM arguments          | validate guest ABI arguments
     |                              | convert slots/byte spans
     +--------------+---------------+
                    |
                  PF2_*
                    |
              MVDSV engine
```

QCX must call/refactor normal `PF2_*` C functions directly. It must not manufacture a PR2 trap argument array and call `PR2_GameSystemCalls()`.

`PF2_*` signatures are internal implementation interfaces, not frozen ABI. A signature may be changed when that makes a hidden PR2 input/output explicit and lets PR2 and QCX share one implementation without splitting the implementation into artificial fragments.

## Global constraints

- QCX continues to require `USE_PR2`.
- PR1 must continue to build and run when `USE_PR2=OFF` and QCX is disabled.
- Preserve the existing QCX host ABI and its guest pointer, byte-span, entity-slot, size and alignment validation.
- Do not introduce a new `sv_qc.c` / `sv_qc.h` shared layer.
- Do not route QCX through `PR2_GameSystemCalls()`.
- Preserve current QCX observable behavior unless PR2 already implements the same behavior.
- Do not refactor a one-line direct engine call merely to claim PF2 reuse.
- `src/qcx/services.h` should contain QCX declarations only; PR1 implementation details must not be declared there.

## Reuse decisions

### Reuse PR2 directly or after a small internal signature change

World services:

- `setorigin` -> `PF2_setorigin`
- `setsize` -> `PF2_setsize`
- `setmodel` -> `PF2_setmodel`, after making the PR2 model-string write path QCX-aware
- `precache_sound` / `precache_sound2` -> `PF2_precache_sound`
- `precache_model` / `precache_model2` -> `PF2_precache_model`
- `lightstyle` -> `PF2_lightstyle`
- `makestatic` -> `PF2_makestatic`; rely on the existing QCX-aware `PR_GetEntityString` path
- `changelevel` -> `PF2_changelevel(map, "")`; QCX intentionally adopts PR2's
  BSP-existence validation and duplicate-changelevel suppression

Movement services:

- `checkclient` -> change `PF2_checkclient` to take explicit `edict_t *self` and return `edict_t *`
- `walkmove` -> `PF2_walkmove`; PR2 already preserves `pr_global_struct->self`, and QCX binds `pr_global_struct` to the shared QCX globals
- `droptofloor` -> `PF2_droptofloor`
- `pointcontents` -> `PF2_pointcontents`

Network services:

- `stuffcmd` -> `PF2_stuffcmd(entnum, text, 0)` after QCX validation
- `centerprint` -> `PF2_centerprint`
- `logfrag` -> `PF2_logfrag`
- `multicast` -> `PF2_multicast`
- `WriteByte`, `WriteChar`, `WriteShort`, `WriteLong`, `WriteCoord`, `WriteAngle`, `WriteString`, `WriteEntity` -> add an explicit `edict_t *msg_entity` parameter to the PF2 functions so PR2 and QCX can supply the `MSG_ONE` target without hidden global lookup
- `setspawnparms` -> change PF2 to take an explicit output array rather than writing directly to `pr_global_struct->parm1`
- `infokey` -> make the PF2 lookup return the selected string; PR2 and QCX keep their own output-buffer conventions

### Keep QCX implementations separate

Keep the existing QCX implementation when the PR2 helper is VM-output-specific, when the QCX ABI naturally maps directly to an engine primitive, or when behavior differs:

- `sound`
- `spawn`
- `remove`
- `traceline`
- `bprint`
- `sprint` (PR2 suppresses output below `cs_connected`)
- `dprint` (PR2 uses `Con_DPrintf`, QCX currently uses `Con_Printf`)
- `checkbottom`
- `aim`
- `cvar`
- `localcmd`
- `step_direction`
- `map_metadata`
- `map_admit`
- `map_time`
- `map_post_spawn`
- `cvar_set` (QCX only changes an already existing cvar)
- `ambientsound` (the current QCX missing-precache failure behavior differs from PR2)

Small direct QCX implementations are acceptable; eliminating every duplicated line is not a goal.

### PR2 API-version isolation

`PR2_InitProg()` accepts game API version 16 or newer.  Shared `PF2_*`
helpers therefore must not inspect `gamedata.APIversion` or retain obsolete
pre-15 compatibility branches: they may be called by QCX as well as by the
PR2 syscall dispatcher.  No pre-15 compatibility branch remains in the
dispatcher either, because such a game cannot pass PR2 initialization.

## PR1 cleanup

After all QCX callers stop using `SV_QC_*`, remove those declarations from `src/qcx/services.h` and remove the `qcx/services.h` dependency from `src/pr_cmds.c`.

The former shared `SV_QC_*` implementations in `pr_cmds.c` should become file-local PR1 implementation details. Prefer renaming them to `PR1_*` static helpers where keeping a helper avoids duplicating a large body; fold only trivial helpers directly into their `PF_*` builtin. Do not split the bodies into new shared fragments.

## Testing strategy

Keep the existing QCX service tests as behavior locks. They may stub the PF2 boundary where linking the whole `pr2_cmds.c` would make a focused test unnecessarily large; the full MVDSV/QC2CPP acceptance matrix is the end-to-end proof that the real PF2 implementation is used correctly.

Validation must cover:

- focused `qcx_services_world`, `qcx_reentry`, and `qcx_client` tests;
- the complete QCX unit/route test set;
- a non-QCX `USE_PR2=OFF` build to prove PR1 remains independent;
- the configured native/Wasm qc2cpp acceptance matrix;
- refresh of the pinned MVDSV commit and tracked source hashes in qc2cpp release-verification inputs after the MVDSV refactor is finalized.
