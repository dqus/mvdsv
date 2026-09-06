# QCX PR2 Builtin Reuse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make QCX reuse PR2 builtin implementations where semantics match, remove the PR1-owned `SV_QC_*` bridge from the QCX interface, and keep QCX-only behavior separate where reuse would change semantics.

**Architecture:** QCX remains an ABI/validation adapter: it validates guest byte spans, vectors, output capacities and entity slots, then calls normal `PF2_*` C functions. PR2's VM syscall dispatcher becomes the other adapter to the same `PF2_*` implementation. Hidden PR2 inputs/outputs may become explicit PF2 parameters when that removes real duplication; there is no new `sv_qc` layer and QCX never calls `PR2_GameSystemCalls()`.

**Tech Stack:** C11, MVDSV PR1/PR2 runtime, qc2cpp QCX native/Wasm host ABI, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-09-06-qcx-pr2-builtin-reuse-design.md`

## Global Constraints

- Code baseline is commit `c354f68a7c2ceb60ae9098b53018956ab82f2d61`; it must remain an ancestor of the implementation branch.
- QCX continues to require `USE_PR2`.
- PR1 must still configure and build with `USE_PR2=OFF` and all QCX options disabled.
- Preserve the current QCX host ABI and all guest pointer/span/entity/size validation.
- Call `PF2_*` directly; never synthesize `PR2_GameSystemCalls()` trap arguments.
- PF2 signatures may change when the change exposes a hidden PR2 input/output used by both PR2 and QCX.
- Do not introduce `sv_qc.c`, `sv_qc.h`, or another shared service layer.
- Leave `sprint`, `dprint`, `changelevel`, `ambientsound`, `traceline`, `cvar_set`, map services and trivial one-line engine operations QCX-specific where the current behavior or output contract differs.
- Do not weaken or remove existing QCX route/acceptance checks merely because implementation moves into PR2.

---

## File Map

- `src/pr2.h`: declarations for PF2 operations intentionally shared with QCX.
- `src/pr2_cmds.c`: shared builtin implementations plus the PR2 syscall adapter; explicit-input/output signature changes live here.
- `src/pr_cmds.c`: PR1-only builtin implementation after the obsolete QCX bridge is removed.
- `src/qcx/services.h`: QCX binders/test observer declarations only.
- `src/qcx/services_world.c`: QCX world ABI validation/conversion and PR2 calls.
- `src/qcx/services_movement.c`: QCX movement ABI validation/conversion and PR2 calls.
- `src/qcx/services_network.c`: QCX network ABI validation/conversion and PR2 calls where semantics match.
- `tests/qcx/services_world_test.c`: focused world adapter behavior/routing lock.
- `tests/qcx/reentry_test.c`: movement/reentry/self-restoration lock.
- `tests/qcx/client_test.c`: network routing/output lock.
- `tests/qcx/CMakeLists.txt`: only change if a focused test needs compile/link options for the new PF2 boundary.
- `tests/qcx/host-imports.tsv`: behavior descriptions stay unchanged unless wording still says an import uses the legacy PR1 bridge.
- `qc2cpp/tests/integration/external-revisions.json`: final MVDSV commit pin.
- `qc2cpp/tests/integration/mvdsv/source-sha256.tsv`: refresh hashes for tracked MVDSV files changed by this refactor.

---

### Task 0: Baseline and Behavior Lock

**Files:** none.

**Interfaces:**
- Consumes: current MVDSV branch and the already configured QCX test build tree, named `build` below.
- Produces: verified clean baseline and a known-green focused test set before refactoring.

- [ ] **Step 1: Verify the implementation starts from the reviewed baseline**

```bash
git status --short
git merge-base --is-ancestor c354f68a7c2ceb60ae9098b53018956ab82f2d61 HEAD
```

Expected: clean status and exit code `0` from `merge-base --is-ancestor`.

- [ ] **Step 2: Build the focused QCX tests before changing code**

```bash
cmake --build build --target \
  qcx_services_world_test \
  qcx_reentry_test \
  qcx_client_test
```

Expected: all three targets build successfully.

- [ ] **Step 3: Run the focused QCX tests**

```bash
ctest --test-dir build \
  -R '^(qcx_services_world|qcx_reentry|qcx_client)$' \
  --output-on-failure
```

Expected: all three tests pass.

- [ ] **Step 4: Verify PR1 still has a standalone non-PR2 build before the refactor**

```bash
cmake -S . -B build-pr1 \
  -DUSE_PR2=OFF \
  -DMVDSV_QC2CPP_NATIVE=OFF \
  -DMVDSV_QC2CPP_WASM=OFF \
  -DMVDSV_QC2CPP_TESTS=OFF
cmake --build build-pr1
```

Expected: configure and build succeed without QCX or PR2.

- [ ] **Step 5: Record the bridge surface that must disappear**

```bash
git grep -n 'SV_QC_' -- src/qcx src/pr_cmds.c
```

Expected before refactoring: declarations in `src/qcx/services.h`, QCX calls in `services_*.c`, and implementations/calls in `src/pr_cmds.c`.

No commit for Task 0.

---

### Task 1: Reuse PR2 for World Services

**Files:**
- Modify: `src/pr2.h`
- Modify: `src/pr2_cmds.c`
- Modify: `src/qcx/services_world.c`
- Modify: `tests/qcx/services_world_test.c`
- Test: `qcx_services_world`

**Interfaces:**
- Produces these shared PF2 declarations in `src/pr2.h`:

```c
void PF2_setorigin(edict_t *entity, float x, float y, float z);
void PF2_setsize(edict_t *entity,
                 float min_x, float min_y, float min_z,
                 float max_x, float max_y, float max_z);
void PF2_setmodel(edict_t *entity, char *model);
void PF2_precache_sound(char *name);
void PF2_precache_model(char *name);
void PF2_lightstyle(int style, char *value);
void PF2_makestatic(edict_t *entity);
```

- QCX still owns byte-span validation and persistent copies required because PR2 stores supplied precache/lightstyle strings.
- `changelevel` deliberately does **not** switch to `PF2_changelevel()` in this task because PR2 adds a BSP-existence check that changes current QCX behavior.

- [ ] **Step 1: Change the focused world test to expect calls through PF2 instead of `SV_QC_*`**

Replace the `SV_QC_SetModel`, `SV_QC_PrecacheSound`, `SV_QC_PrecacheModel`, `SV_QC_LightStyle`, and `SV_QC_MakeStatic` test doubles with PF2 test doubles. Add PF2 doubles for `setorigin` and `setsize` so the test proves the QCX wrapper crosses the PR2 boundary after validation.

The PF2 test doubles must record arguments and apply only the minimum observable state needed by the existing assertions. For model, the double must perform the semantic model write through the existing QCX test hook before setting `modelindex`, because production `PF2_setmodel()` will own that write after this task.

- [ ] **Step 2: Run the world test and verify the new expectation fails**

```bash
cmake --build build --target qcx_services_world_test
ctest --test-dir build -R '^qcx_services_world$' --output-on-failure
```

Expected: build/link/test fails because production `services_world.c` still references the old bridge or still implements setorigin/setsize itself.

- [ ] **Step 3: Publish the intentionally shared PF2 world surface in `src/pr2.h`**

Add exactly the seven declarations listed in **Interfaces**. Do not expose unrelated PF2 helpers yet.

- [ ] **Step 4: Make the PR2 model-string path QCX-aware without changing normal PR2 VM behavior**

Add an explicit QCX dependency at the top of `src/pr2_cmds.c` rather than relying on a transitive include:

```c
#ifdef QCX_ENABLED
#include "qcx/adapter.h"
#endif
```

At the start of `PR2_SetEntityString_model()` in `src/pr2_cmds.c`, add:

```c
#ifdef QCX_ENABLED
	if (QCX_Active()) {
		PR2_SetEntityString(ed, target, s);
		return;
	}
#endif
```

Keep the existing `!sv_vm`, native-reference, bytecode and compiled-VM cases unchanged below it.

- [ ] **Step 5: Route QCX world imports through PF2**

Implement the wrappers as follows after their existing QCX validation:

```c
PF2_setorigin(entity, origin[0], origin[1], origin[2]);

PF2_setsize(entity,
	mins[0], mins[1], mins[2],
	maxs[0], maxs[1], maxs[2]);

PF2_setmodel(entity, local_model);

PF2_precache_model(persistent_name);
PF2_precache_sound(persistent_name);

PF2_lightstyle((int)style, persistent_value);

PF2_makestatic(entity);
```

Specific simplifications:

- `QCX_SetModel()` must no longer call `QCX_SetEntityString()` itself; `PF2_setmodel()` now owns the model-field update through the QCX-aware PR2 string path.
- `QCX_Precache()` changes its operation type from `int (*)(const char *)` to `void (*)(char *)`; QCX still creates the persistent string and copies the result back to the guest output buffer.
- `QCX_MakeStatic()` removes the local model copy; `PF2_makestatic()` resolves `ent->v->model` through the already QCX-aware `PR_GetEntityString()` path.
- Keep `QCX_ChangeLevel()` behavior unchanged for now; it must not call `PF2_changelevel()`.

- [ ] **Step 6: Run the focused world test**

```bash
cmake --build build --target qcx_services_world_test
ctest --test-dir build -R '^qcx_services_world$' --output-on-failure
```

Expected: PASS with the same externally observed host-service behavior.

- [ ] **Step 7: Commit Task 1**

```bash
git add src/pr2.h src/pr2_cmds.c src/qcx/services_world.c \
  tests/qcx/services_world_test.c
git commit -m 'refactor: reuse PR2 world builtins for QCX'
```

---

### Task 2: Reuse PR2 for Movement Services

**Files:**
- Modify: `src/pr2.h`
- Modify: `src/pr2_cmds.c`
- Modify: `src/qcx/services_movement.c`
- Modify: `tests/qcx/reentry_test.c`
- Test: `qcx_reentry`

**Interfaces:**
- Add/standardize:

```c
edict_t *PF2_checkclient(edict_t *self);
int PF2_walkmove(edict_t *entity, float yaw, float distance);
int PF2_droptofloor(edict_t *entity);
int PF2_pointcontents(float x, float y, float z);
```

- PR2 syscall conversion remains PR2-owned; QCX slot conversion remains QCX-owned.
- `traceline`, `checkbottom`, `aim`, and `step_direction` stay in their current QCX forms.

- [ ] **Step 1: Change the reentry test doubles from `SV_QC_*` to the PF2 boundary**

Provide focused test doubles for:

```c
edict_t *PF2_checkclient(edict_t *self);
int PF2_walkmove(edict_t *entity, float yaw, float distance);
int PF2_droptofloor(edict_t *entity);
int PF2_pointcontents(float x, float y, float z);
```

Keep the existing assertions that nested walkmove may change `other` and `time` but the caller's canonical `self` is restored.

- [ ] **Step 2: Run the movement test and verify the new expectation fails**

```bash
cmake --build build --target qcx_reentry_test
ctest --test-dir build -R '^qcx_reentry$' --output-on-failure
```

Expected: failure until `services_movement.c` is routed through PF2.

- [ ] **Step 3: Make `PF2_checkclient` explicit and representation-neutral**

Change:

```c
intptr_t PF2_checkclient(void);
```

to:

```c
edict_t *PF2_checkclient(edict_t *self);
```

Inside the function:

- remove the read of `pr_global_struct->self`;
- use the explicit `self` argument for the PVS calculation;
- return `edict_t *` (`ent` or the world edict) instead of an encoded entity number.

Update the PR2 syscall case to perform the representation conversion at the adapter boundary:

```c
case G_CHECKCLIENT:
	return NUM_FOR_EDICT(
		PF2_checkclient(PROG_TO_EDICT(pr_global_struct->self)));
```

- [ ] **Step 4: Route QCX movement imports through PF2**

Use:

```c
edict_t *result = PF2_checkclient(QCX_RequireMovementEdict(self));

const qcx_entity_id_t caller_self = globals->self;
const float result = (float)PF2_walkmove(
	QCX_RequireMovementEdict(caller_self), yaw, distance);
globals->self = caller_self;

return (float)PF2_droptofloor(QCX_RequireMovementEdict(self));

return (float)PF2_pointcontents(point[0], point[1], point[2]);
```

Keep the explicit QCX `globals->self = caller_self` assignment even though PF2 currently restores the same shared global; it is a cheap QCX boundary guarantee and keeps the existing reentry contract obvious.

- [ ] **Step 5: Run the focused movement test**

```bash
cmake --build build --target qcx_reentry_test
ctest --test-dir build -R '^qcx_reentry$' --output-on-failure
```

Expected: PASS, including the existing self/other/time reentry assertions.

- [ ] **Step 6: Commit Task 2**

```bash
git add src/pr2.h src/pr2_cmds.c src/qcx/services_movement.c \
  tests/qcx/reentry_test.c
git commit -m 'refactor: reuse PR2 movement builtins for QCX'
```

---

### Task 3: Reuse PR2 Network Operations with Matching Semantics

**Files:**
- Modify: `src/pr2.h`
- Modify: `src/qcx/services_network.c`
- Modify: `tests/qcx/client_test.c`
- Test: `qcx_client`

**Interfaces:**
- Expose:

```c
void PF2_stuffcmd(int entnum, char *text, int flags);
void PF2_centerprint(int entnum, char *text);
void PF2_logfrag(int killer_entnum, int victim_entnum);
void PF2_multicast(float x, float y, float z, int destination);
```

- QCX passes `flags = 0` to `PF2_stuffcmd`; PR2-only extension flags are out of QCX scope.
- `sound`, `bprint`, `sprint`, `dprint`, and `ambientsound` remain QCX implementations.

- [ ] **Step 1: Change the client test to observe the PF2 boundary for the four migrated operations**

Replace only the duplicated implementations under test for `stuffcmd`, `centerprint`, `logfrag`, and `multicast` with PF2 test doubles that record the validated arguments. Keep all existing detailed behavior tests for network imports that remain QCX-owned.

- [ ] **Step 2: Run the client test and verify the new routing expectation fails**

```bash
cmake --build build --target qcx_client_test
ctest --test-dir build -R '^qcx_client$' --output-on-failure
```

Expected: failure until the four QCX wrappers call PF2.

- [ ] **Step 3: Expose the four PF2 functions in `src/pr2.h`**

Add exactly the declarations from **Interfaces**.

- [ ] **Step 4: Replace duplicated QCX bodies with validation plus PF2 calls**

After existing QCX byte-span/entity validation, call:

```c
PF2_stuffcmd(NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity)), local, 0);
PF2_centerprint(NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity)), local);
PF2_logfrag(NUM_FOR_EDICT(QCX_RequireNetworkEdict(killer)),
	NUM_FOR_EDICT(QCX_RequireNetworkEdict(victim)));
PF2_multicast(origin[0], origin[1], origin[2], (int)destination);
```

Do not move QCX validation into PR2.

- [ ] **Step 5: Run the focused client test**

```bash
cmake --build build --target qcx_client_test
ctest --test-dir build -R '^qcx_client$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit Task 3**

```bash
git add src/pr2.h src/qcx/services_network.c tests/qcx/client_test.c
git commit -m 'refactor: reuse matching PR2 network builtins for QCX'
```

---

### Task 4: Make PR2 Message and Output Dataflow Explicit

**Files:**
- Modify: `src/pr2.h`
- Modify: `src/pr2_cmds.c`
- Modify: `src/qcx/services_network.c`
- Modify: `tests/qcx/client_test.c`
- Test: `qcx_client`

**Interfaces:**

Message writers become:

```c
void PF2_WriteByte(int to, int value, edict_t *msg_entity);
void PF2_WriteChar(int to, int value, edict_t *msg_entity);
void PF2_WriteShort(int to, int value, edict_t *msg_entity);
void PF2_WriteLong(int to, int value, edict_t *msg_entity);
void PF2_WriteCoord(int to, float value, edict_t *msg_entity);
void PF2_WriteAngle(int to, float value, edict_t *msg_entity);
void PF2_WriteString(int to, char *value, edict_t *msg_entity);
void PF2_WriteEntity(int to, int value_entnum, edict_t *msg_entity);
```

Spawn parms become:

```c
void PF2_setspawnparms(int entnum, float out_parms[NUM_SPAWN_PARMS]);
```

Infokey lookup becomes:

```c
const char *PF2_infokey(int entnum, const char *key);
```

The returned infokey pointer is borrowed for immediate copy only; existing PR2 static/internal storage lifetime remains unchanged.

- [ ] **Step 1: Add focused client-test expectations for explicit `msg_entity`, spawn-parm output, and infokey lookup**

The test must prove:

- `MSG_ONE` uses the QCX-supplied entity rather than an implicit global target;
- non-`MSG_ONE` writes do not require a message entity;
- `setspawnparms` writes the supplied output array;
- QCX infokey still returns the required byte count and truncates the copy to guest capacity without requiring a trailing NUL.

- [ ] **Step 2: Run the client test and verify it fails against the old hidden-state signatures**

```bash
cmake --build build --target qcx_client_test
ctest --test-dir build -R '^qcx_client$' --output-on-failure
```

Expected: build/test failure until PF2 signatures and both adapters are updated.

- [ ] **Step 3: Make the PR2 message target explicit**

Change `Write_GetClient()` to:

```c
static client_t *Write_GetClient(edict_t *entity)
{
	if (entity == NULL) {
		PR2_RunError("WriteDest: MSG_ONE requires a client entity");
	}
	const int entnum = NUM_FOR_EDICT(entity);
	if (entnum < 1 || entnum > MAX_CLIENTS) {
		PR2_RunError("WriteDest: not a client");
	}
	return &svs.clients[entnum - 1];
}
```

Add the explicit `edict_t *msg_entity` argument to all eight PF2 writer signatures and use it only on the `MSG_ONE` path.

Update every PR2 syscall case to pass:

```c
PROG_TO_EDICT(pr_global_struct->msg_entity)
```

as the new final PF2 argument. Preserve the existing `WriteDest2()` handling for all non-`MSG_ONE` destinations.

- [ ] **Step 4: Route QCX Write* imports through the explicit PF2 writers**

QCX keeps the exact-float-to-int destination check. For `MSG_ONE`, resolve the supplied `msg_entity` slot to an edict before the PF2 call. For other destinations pass `NULL` as `msg_entity`.

Examples:

```c
PF2_WriteByte(to, (int)value, msg_entity_edict);
PF2_WriteCoord(to, value, msg_entity_edict);
PF2_WriteString(to, local, msg_entity_edict);
PF2_WriteEntity(to, NUM_FOR_EDICT(QCX_RequireNetworkEdict(value)), msg_entity_edict);
```

After this step, remove the duplicated QCX `QCX_WriteDestination`, `QCX_CoordMessageSize`, `QCX_AngleMessageSize`, and `QCX_DEFINE_WRITE` routing code if they have no remaining callers.

- [ ] **Step 5: Make spawn-parm output explicit**

Change `PF2_setspawnparms()` to copy client spawn parms to the supplied `out_parms` array instead of `&pr_global_struct->parm1`.

PR2 syscall call site:

```c
PF2_setspawnparms(args[1], &pr_global_struct->parm1);
```

QCX call site after its existing output-size validation:

```c
PF2_setspawnparms(NUM_FOR_EDICT(QCX_RequireNetworkEdict(entity)), out_parms);
```

- [ ] **Step 6: Separate infokey lookup from each adapter's output convention**

Change `PF2_infokey()` so its existing lookup logic returns `const char *value` and no longer takes/writes `valbuff` or `sizebuff`.

PR2 syscall path keeps its VM bounds check, then performs:

```c
const char *value = PF2_infokey(args[1], VMA(2));
strlcpy(VMA(3), value, args[4]);
```

QCX performs:

```c
const char *value = PF2_infokey(entnum, local_key);
const qcx_byte_count_t required = (qcx_byte_count_t)strlen(value);
if (out != NULL && out_capacity != 0U) {
	const qcx_byte_count_t copied = required < out_capacity ? required : out_capacity;
	memcpy(out, value, copied);
}
return required;
```

This preserves QCX's size-query/truncating byte-span ABI and PR2's NUL-terminated VM-buffer ABI.

- [ ] **Step 7: Run the focused client test**

```bash
cmake --build build --target qcx_client_test
ctest --test-dir build -R '^qcx_client$' --output-on-failure
```

Expected: PASS with existing QCX message, spawn-parm, infokey, MVD and destination behavior preserved.

- [ ] **Step 8: Commit Task 4**

```bash
git add src/pr2.h src/pr2_cmds.c src/qcx/services_network.c \
  tests/qcx/client_test.c
git commit -m 'refactor: make PR2 message service inputs explicit'
```

---

### Task 5: Remove the PR1 QCX Bridge and Run Full Acceptance

**Files:**
- Modify: `src/qcx/services.h`
- Modify: `src/pr_cmds.c`
- Modify: `tests/qcx/host-imports.tsv` only if any description still names the old PR1 bridge
- Modify in qc2cpp after the final MVDSV commit: `tests/integration/external-revisions.json`
- Modify in qc2cpp after the final MVDSV commit: `tests/integration/mvdsv/source-sha256.tsv`

**Interfaces:**
- `src/qcx/services.h` produces only:

```c
void QCX_BindWorldServices(qcx_host_api_v1_t *host);
void QCX_BindMovementServices(qcx_host_api_v1_t *host);
void QCX_BindNetworkServices(qcx_host_api_v1_t *host);
```

plus the existing `QCX_ObserveGameplayImport` test macro.
- No `SV_QC_*` symbol remains part of the QCX interface.

- [ ] **Step 1: Remove all `SV_QC_*` declarations from `src/qcx/services.h`**

Delete the two comment blocks and all twelve `SV_QC_*` declarations. Keep the three binder declarations and the observer macro unchanged.

- [ ] **Step 2: Make the old bridge implementations PR1-local**

Remove:

```c
#ifdef QCX_ENABLED
#include "qcx/services.h"
#endif
```

from `src/pr_cmds.c`.

For the former `SV_QC_*` functions that PR1 still uses:

- fold only trivial `setorigin` / `setsize` bodies directly into their `PF_*` builtins if that is shorter;
- otherwise rename the helper to a file-local `static PR1_*` function and keep its body intact.

Do not move PR1 logic into PR2 and do not create a new common file. In particular, keep PR1/NQ compatibility behavior in PR1.

- [ ] **Step 3: Verify the bridge is gone**

```bash
if git grep -n 'SV_QC_' -- src/qcx src/pr_cmds.c; then
  echo 'obsolete SV_QC bridge still present' >&2
  exit 1
fi
```

Expected: no matches.

- [ ] **Step 4: Run focused tests**

```bash
cmake --build build --target \
  qcx_services_world_test \
  qcx_reentry_test \
  qcx_client_test
ctest --test-dir build \
  -R '^(qcx_services_world|qcx_reentry|qcx_client)$' \
  --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Run the complete QCX test set**

```bash
cmake --build build
ctest --test-dir build -R '^qcx_' --output-on-failure
```

Expected: all QCX tests pass.

- [ ] **Step 6: Rebuild PR1 without PR2/QCX**

Use a fresh tree so cached QCX/PR2 definitions cannot mask the dependency:

```bash
rm -rf build-pr1
cmake -S . -B build-pr1 \
  -DUSE_PR2=OFF \
  -DMVDSV_QC2CPP_NATIVE=OFF \
  -DMVDSV_QC2CPP_WASM=OFF \
  -DMVDSV_QC2CPP_TESTS=OFF
cmake --build build-pr1
```

Expected: configure and build succeed.

- [ ] **Step 7: Run the configured qc2cpp acceptance matrix in MVDSV**

On the existing full acceptance build tree (the one configured with `QC2CPP_COMPILER`, `QC2CPP_CHECKER`, `QC2CPP_QW_MANIFEST`, `QC2CPP_ASSET_DIR`, and, for Wasm, the existing WASI/Wasmtime inputs), run:

```bash
cmake --build build --target qc2cpp_acceptance_assets
ctest --test-dir build -R '^qc2cpp_' --output-on-failure
```

At minimum the matrix must include the existing native/Wasm map, legacy-string and save cross-mode tests; do not narrow the configured suite for this refactor.

- [ ] **Step 8: Commit the MVDSV cleanup only after all MVDSV tests are green**

```bash
git add src/qcx/services.h src/pr_cmds.c tests/qcx/host-imports.tsv
git commit -m 'refactor: remove PR1 QCX service bridge'
```

If `host-imports.tsv` needed no wording change, leave it out of the commit.

- [ ] **Step 9: Refresh qc2cpp's pinned MVDSV verification input**

In the qc2cpp `main` checkout, set `tests/integration/external-revisions.json` `mvdsv.commit` to the final MVDSV commit from Step 8.

Refresh SHA-256 rows for every already-tracked MVDSV source changed by this refactor. This will include `src/pr2_cmds.c` and `src/pr_cmds.c`; refresh any other tracked file only if it actually changed. Do not expand the manifest merely to mirror the whole MVDSV diff.

- [ ] **Step 10: Run qc2cpp verification after the pin refresh**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

If the release-verification build is separate, run the existing release-verification target/test on that configured tree as well; it must accept the exact final MVDSV commit and refreshed hashes.

- [ ] **Step 11: Commit the qc2cpp pin refresh**

```bash
git add tests/integration/external-revisions.json \
  tests/integration/mvdsv/source-sha256.tsv
git commit -m 'test: refresh PR2-backed mvdsv verification input'
```

---

## Completion Criteria

The refactor is complete when all of the following are true:

```text
QCX host ABI validation
        |
        +--> PF2 shared builtin implementation where semantics match
        |
        +--> direct QCX implementation where semantics differ

PR2 VM syscall adapter --> same PF2 shared implementation

PR1 --> independent PR1 builtin implementation
```

And:

- `src/qcx/services.h` contains no `SV_QC_*` declarations;
- `src/pr_cmds.c` does not include `qcx/services.h`;
- `git grep 'SV_QC_' -- src/qcx src/pr_cmds.c` is empty;
- no QCX code calls `PR2_GameSystemCalls()`;
- focused world/movement/network tests pass;
- all `qcx_*` tests pass;
- `USE_PR2=OFF` non-QCX build passes;
- configured qc2cpp native/Wasm acceptance passes;
- qc2cpp release verification pins the final MVDSV commit.
