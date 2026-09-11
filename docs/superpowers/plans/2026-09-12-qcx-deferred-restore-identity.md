# QCX Deferred Restore Identity Commit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Stop for code review after every task and resolve findings before starting the next task.

**Goal:** Replace the current rollback-based QCX roster handoff with a deferred-commit model: a `BOUND` roster entry is only a pending overlay, and saved role/team/spawn parameters become authoritative in `client_t` only when the client successfully reaches `begin`.

**Architecture:** Keep QCMS V2, roster state, slot reservations, client-slot permutation, restore pause, fresh-map restore, and the existing QCX ABI unchanged. During `BOUND`, the network connection keeps its connection-owned state while the restore session supplies only the saved role needed by the signon protocol. `Cmd_Begin_f()` becomes the single commit point for saved gameplay identity; timeout/cancel discards a binding rather than rolling fields back.

**Tech Stack:** C11 MVDSV server code, CMake/CTest unit and route-contract tests, Python process acceptance tests, qc2cpp integration revision manifests.

**Spec:** `qc2cpp/docs/superpowers/specs/2026-09-10-qcx-roster-restore-design.md`

## Repository Baselines

- `dqus/mvdsv`, branch `codex/qcx-roster-restore`, reviewed baseline: `ad37e661d8a9b3619fcd0e339c002769148a54df`
- `dqus/qc2cpp`, branch `codex/qcx-roster-restore`, reviewed baseline: `e7a6c7394a9b55c32cb53bab3ae0743935ccb641`

Before executing Task 1, refresh both branch heads. If either branch advanced, review the new diff and update the baseline notes before editing.

## Global Constraints

- Do not change QCMS V2 layout or version.
- Do not change the QCX ABI.
- Do not split `client_t` into separate connection/game structures in this work.
- Do not redesign client-slot permutation.
- Preserve the QuakeWorld invariant `client slot N -> player edict N + 1`.
- Keep roster states `available -> bound -> active` / `abandoned`.
- Keep `qcx_restore_waiting`, `qcx_restore_pending`, and `qcx_restore_roster_index`.
- Keep fresh-map restore as the only QCX load path.
- Keep `SV_PAUSE_RESTORE` independent of manual pause.
- A fresh connection that is allowed to restore a saved role must be authenticated for that saved role before admission.
- A fresh connection may fall back to its originally requested role after abandonment only if that requested role was independently authenticated.
- Existing/carried live clients are already admitted; their ordinary fallback is allowed.
- `BOUND` must not overwrite `client->spectator`, `client->team`, team/spectator userinfo, or `client->spawn_parms`.
- `ACTIVE` identity is committed and must never be rolled back by timeout, cancel, map change, shutdown, or replacement load.
- QWSP functional acceptance uses `e1m2` with `deathmatch 0`.
- Run acceptance for both Native and Wasm QCX transports.
- Do not add speculative abstractions outside the restore/admission paths.

---

### Task 1: Update the qc2cpp design spec to deferred commit

**Repository:** `qc2cpp`

**Files:**
- Modify: `docs/superpowers/specs/2026-09-10-qcx-roster-restore-design.md`

**Interfaces:**
- Consumes: current roster design and terminology.
- Produces: authoritative semantics used by all later MVDSV tasks.

- [ ] **Step 1: Rewrite the runtime identity lifecycle**

Replace the current requirement that saved role/team are written before restore-handshake replication with this model:

```text
available
  -> bound
       connection-owned role/team/spawn parms remain unchanged
       saved roster identity is a pending overlay
       saved role may be exposed to signon protocol where required
  -> active
       successful begin commits saved role/team/spawn parms to client_t
```

State explicitly:

```text
BOUND -> abandoned/cancelled:
    discard binding
    do not restore fields because no gameplay identity was overwritten

ACTIVE -> map/cancel/session-complete:
    keep committed saved identity
```

- [ ] **Step 2: Specify the `saved.spawned` behavior**

Add:

```text
saved.spawned == 1:
    restored edict/gameplay state already exists
    spawn/begin must not call ClientConnect/PutClientInServer again

saved.spawned == 0:
    saved identity is still committed at begin
    then ordinary ClientConnect/PutClientInServer runs with the committed
    saved role/team/spawn parms
```

- [ ] **Step 3: Specify authentication independently from gameplay identity**

Add:

```text
DirectConnect evaluates two roles without mutating userinfo:

1. saved_role_allowed
   - required to claim the roster entry

2. requested_role_allowed
   - required only if the connection later falls back after abandonment

If saved_role_allowed is false:
    reject the saved-role admission.

If saved_role_allowed is true and requested_role_allowed is false:
    allow restore,
    but drop the connection if that restore is abandoned before begin.
```

State that authentication evaluation must not send network diagnostics or mutate the supplied userinfo. Normalization/diagnostics are separate admission actions.

- [ ] **Step 4: Self-review the spec**

Check for and remove all wording that still requires:

```text
pre-begin mutation of client->spectator
pre-begin mutation of team userinfo
pre-begin mutation of spawn parms
rollback of an "original identity" snapshot
```

- [ ] **Step 5: Commit**

```bash
git add docs/superpowers/specs/2026-09-10-qcx-roster-restore-design.md
git commit -m "docs: defer roster identity commit until begin"
```

**Review gate:** Review only the spec change. Do not begin MVDSV implementation until the deferred-commit semantics and authentication rules are approved.

---

### Task 2: Separate role authentication from userinfo normalization

**Repository:** `mvdsv`

**Files:**
- Modify: `src/sv_main.c`
- Modify: `src/server.h`
- Modify: `tests/qcx/restore_routes_test.cmake`
- Modify: `tests/integration/qc2cpp_acceptance.py`

**Interfaces:**
- Produces:
  - a side-effect-free role evaluator used by `SVC_DirectConnect`;
  - one connection-level fallback authorization bit:
    ```c
    qbool qcx_restore_fallback_allowed;
    ```
- Consumes: existing password/VIP configuration and `CheckPasswords()` semantics.

- [ ] **Step 1: Add failing process tests for asymmetric passwords**

Add Native/Wasm process cases where the requested role and saved role have different authorization outcomes.

Required cases:

```text
A. saved player allowed, requested spectator denied
   -> exact-name restore admission succeeds
   -> if restore is abandoned before begin, connection is dropped

B. saved spectator allowed, requested player denied
   -> exact-name restore admission succeeds
   -> if restore is abandoned before begin, connection is dropped

C. saved role denied
   -> exact-name restore admission is rejected
```

Use the existing server password/spectator-password mechanisms; do not invent test-only admission rules.

- [ ] **Step 2: Run the acceptance case and verify failure**

Run the focused acceptance entry point that contains QCX roster restore tests.

Expected: at least the asymmetric-role cases fail because current code authenticates only the role produced by mutated userinfo.

- [ ] **Step 3: Extract a side-effect-free role evaluator**

Refactor the logic currently embedded in `CheckPasswords()` so the authorization decision can be evaluated for an explicit target role without:

```text
Info_RemoveKey()
Info_SetValueForStarKey()
Netchan_OutOfBandPrint()
Con_Printf() failure diagnostics
```

Define a small internal result in `sv_main.c`:

```c
typedef struct sv_role_admission_s {
    qbool allowed;
    qbool vip;
} sv_role_admission_t;

static sv_role_admission_t SV_EvaluateRoleAdmission(
    const char *userinfo,
    qbool spectator_role);
```

Requirements:

```text
spectator_role == false:
    reproduce current player password/VIP acceptance semantics

spectator_role == true:
    reproduce current spectator password/VIP acceptance semantics
```

The evaluator may read server cvars, `net_from`, `SV_VIPbyPass()`, and `SV_VIPbyIP()`, but must not mutate userinfo or emit client-visible output.

- [ ] **Step 4: Keep ordinary non-restore `CheckPasswords()` behavior unchanged**

Rewrite `CheckPasswords()` as a wrapper around the evaluator plus the existing:

```text
failure diagnostics
userinfo credential removal
*spectator normalization
spass/vip/spectator outputs
```

Run existing non-QCX tests to ensure ordinary admission semantics did not change.

- [ ] **Step 5: Evaluate saved and requested roles before restore admission**

In `SVC_DirectConnect()` while a restore session is waiting:

```c
requested_spectator = /* derive from untouched incoming userinfo */;
saved_role = QCX_RestoreSessionAdmissionRole(...);

requested_auth = SV_EvaluateRoleAdmission(userinfo, requested_spectator);
saved_auth = SV_EvaluateRoleAdmission(userinfo, saved_role);
```

Rules:

```text
no roster match:
    use ordinary CheckPasswords()

roster match && !saved_auth.allowed:
    emit the same class of role/password rejection diagnostic
    reject connection

roster match && saved_auth.allowed:
    admit the saved-role restore
    remember requested_auth.allowed in qcx_restore_fallback_allowed
```

Do not call `QCX_RestoreSessionRememberAdmissionIdentity()`; Task 3 removes that API.

- [ ] **Step 6: Normalize the accepted connection without committing saved gameplay identity**

For a restore admission, sanitize credentials and construct the accepted `client_t` without writing saved team/spawn parms and without relying on a later rollback snapshot.

The accepted `client_t` must retain the requested connection role until Task 5 commits the saved identity. Store only:

```c
newcl->qcx_restore_fallback_allowed = requested_auth.allowed;
```

Existing/carried clients get:

```c
qcx_restore_fallback_allowed = true;
```

when the restore session adopts them.

- [ ] **Step 7: Add route-contract assertions**

Update `tests/qcx/restore_routes_test.cmake` to require:

```text
SV_EvaluateRoleAdmission
qcx_restore_fallback_allowed
saved-role evaluation before roster admission
no call to QCX_RestoreSessionRememberAdmissionIdentity
```

- [ ] **Step 8: Run focused tests**

```bash
cmake --build build
ctest --test-dir build -R '^qcx_restore_(session|routes)$' --output-on-failure
```

Then run the focused Native process cases from Step 1.

- [ ] **Step 9: Commit**

```bash
git add src/sv_main.c src/server.h tests/qcx/restore_routes_test.cmake \
    tests/integration/qc2cpp_acceptance.py
git commit -m "qcx: separate restore admission from requested role"
```

**Review gate:** Verify ordinary `CheckPasswords()` behavior is unchanged and restore admission no longer depends on mutating the user's role before authorization.

---

### Task 3: Make `BOUND` a pure roster overlay and remove rollback snapshots

**Repository:** `mvdsv`

**Files:**
- Modify: `src/qcx/restore_session.c`
- Modify: `src/qcx/restore_session.h`
- Modify: `src/server.h`
- Modify: `tests/qcx/restore_session_test.c`

**Interfaces:**
- Consumes: `qcx_restore_fallback_allowed` from Task 2.
- Produces: pure `BOUND` semantics with no `original_*` snapshot.

- [ ] **Step 1: Add a failing bound-state immutability test**

Create a unit case that records:

```text
client->spectator
client->team
userinfo "*spectator"
userinfo "team"
userinfoshort "*spectator"
userinfoshort "team"
all NUM_SPAWN_PARMS values
```

Then:

```text
install roster
match client
reconcile to BOUND
```

Assert all recorded values are unchanged.

Expected before implementation: FAIL because `QCX_RestoreSessionApplySavedIdentity()` overwrites them.

- [ ] **Step 2: Remove rollback fields from `client_t`**

Delete:

```c
qcx_restore_has_original_identity
qcx_restore_original_spectator
qcx_restore_original_team
qcx_restore_original_spawn_parms
```

Keep:

```c
qcx_restore_waiting
qcx_restore_pending
qcx_restore_roster_index
qcx_restore_fallback_allowed
```

- [ ] **Step 3: Delete rollback APIs**

Delete:

```c
QCX_RestoreSessionRememberAdmissionIdentity()
QCX_RestoreSessionClearClientOriginalIdentity()
QCX_RestoreSessionRestoreOriginalIdentity()
```

Remove all callers.

- [ ] **Step 4: Replace `QCX_RestoreSessionApplySavedIdentity()` with non-mutating bind notification**

Binding must do only:

```text
qcx_restore_pending = true
qcx_restore_roster_index = index
roster entry AVAILABLE -> BOUND
move the connection to the saved physical slot when needed
notify the user which saved identity was selected
```

It must not modify role/team/userinfo/spawn parms.

- [ ] **Step 5: Simplify reset/finish paths enough to compile**

For now, timeout/cancel may simply clear pending binding and start/drop fallback according to `qcx_restore_fallback_allowed`. Task 6 performs the final lifecycle cleanup.

There must be no identity rollback function left.

- [ ] **Step 6: Run unit tests**

```bash
cmake --build build --target qcx_restore_session_test
ctest --test-dir build -R '^qcx_restore_session$' --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add src/qcx/restore_session.c src/qcx/restore_session.h src/server.h \
    tests/qcx/restore_session_test.c
git commit -m "qcx: keep bound restore identity as overlay"
```

**Review gate:** `git grep -n 'qcx_restore_original\|RememberAdmissionIdentity\|RestoreOriginalIdentity' src tests` must return no restore-snapshot implementation references.

---

### Task 4: Expose only the saved role needed by pre-begin signon

**Repository:** `mvdsv`

**Files:**
- Modify: `src/qcx/restore_session.c`
- Modify: `src/qcx/restore_session.h`
- Modify: `src/sv_user.c`
- Modify: `tests/qcx/restore_session_test.c`
- Modify: `tests/qcx/restore_routes_test.cmake`

**Interfaces:**
- Produces:
  ```c
  qbool QCX_RestoreSessionEffectiveSpectator(const client_t *client);
  ```
- Consumes: bound roster entry and unchanged connection-owned `client_t`.

- [ ] **Step 1: Add a failing effective-role test**

Create:

```text
requested spectator, saved player -> BOUND:
    client->spectator remains true
    EffectiveSpectator(client) == false

requested player, saved spectator -> BOUND:
    client->spectator remains false
    EffectiveSpectator(client) == true
```

- [ ] **Step 2: Implement `QCX_RestoreSessionEffectiveSpectator()`**

Behavior:

```c
if client has a BOUND roster entry:
    return entry->saved.role == QCX_SAVE_ROLE_SPECTATOR;
return client->spectator != 0;
```

Do not expose saved team or spawn parms through analogous helpers.

- [ ] **Step 3: Use the helper in `Cmd_New_f()`**

When constructing QW `playernum`, replace the direct role read:

```c
if (sv_client->spectator)
    playernum |= 128;
```

with the effective restore role under `QCX_ENABLED`.

The actual `client->spectator` value remains unchanged until Task 5.

- [ ] **Step 4: Audit the pre-begin path**

Run:

```bash
git grep -n 'spectator' -- src/sv_user.c src/sv_main.c src/sv_send.c
```

For each hit reachable before `Cmd_Begin_f()`:

- admission authorization is handled by Task 2;
- protocol-visible saved role is handled by `EffectiveSpectator`;
- gameplay-only reads remain untouched because gameplay cannot begin while `pending`.

Do not introduce a general-purpose spectator wrapper across MVDSV.

- [ ] **Step 5: Run tests**

```bash
cmake --build build
ctest --test-dir build -R '^qcx_restore_(session|routes)$' --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/qcx/restore_session.c src/qcx/restore_session.h src/sv_user.c \
    tests/qcx/restore_session_test.c tests/qcx/restore_routes_test.cmake
git commit -m "qcx: expose pending restore role to signon"
```

**Review gate:** Verify the only pre-begin saved gameplay value exposed outside the restore session is the role required by signon protocol.

---

### Task 5: Make `Cmd_Begin_f()` the single saved-identity commit point

**Repository:** `mvdsv`

**Files:**
- Modify: `src/qcx/restore_session.c`
- Modify: `src/qcx/restore_session.h`
- Modify: `src/sv_user.c`
- Modify: `tests/qcx/restore_session_test.c`
- Modify: `tests/qcx/restore_routes_test.cmake`

**Interfaces:**
- Produces:
  ```c
  qbool QCX_RestoreSessionCommitBegin(
      client_t *client,
      qbool *restores_spawned_gameplay);
  ```

- [ ] **Step 1: Add state-transition tests**

Required assertions:

```text
before bind:
    requested role/team/spawn parms

after bind:
    requested role/team/spawn parms unchanged

after CommitBegin:
    saved role/team/spawn parms installed
    "*spectator" and "team" userinfo synchronized
    roster entry ACTIVE
    qcx_restore_pending == false
```

Cover both saved player and saved spectator.

- [ ] **Step 2: Add `saved.spawned == 0` callback-path test**

For an unspawned saved roster entry:

```text
CommitBegin commits saved identity
restores_spawned_gameplay == false
ordinary ClientConnect/PutClientInServer path must run
```

For a spawned entry:

```text
restores_spawned_gameplay == true
ordinary gameplay connect/spawn callbacks must be skipped
```

- [ ] **Step 3: Implement `QCX_RestoreSessionCommitBegin()`**

The function must:

```text
require BOUND entry
write saved client->spectator
synchronize "*spectator" in both userinfo contexts
write saved client->team
synchronize "team" in both userinfo contexts
copy all NUM_SPAWN_PARMS
return saved.spawned through restores_spawned_gameplay
activate roster entry
clear pending/waiting
mark session dirty
```

No rollback state is created.

- [ ] **Step 4: Keep `QCX_RestoreSessionPrepareSpawn()` narrow**

It must return true only for:

```text
BOUND && saved.spawned != 0
```

Its job remains connection/network preparation for an already-restored gameplay entity. It must not apply identity.

- [ ] **Step 5: Reorder `Cmd_Begin_f()`**

Required order:

```text
1. reject restore-waiting client
2. identify qcx pending
3. if pending:
       QCX_RestoreSessionCommitBegin(...)
4. if saved gameplay was already spawned:
       skip spectator spawn + ClientConnect + PutClientInServer
   else:
       run ordinary gameplay initialization using the now-committed saved identity
5. finish normal network begin work
```

Do not call the old `QCX_RestoreSessionBegin()`; replace/remove it.

- [ ] **Step 6: Preserve the early `spawn`/`begin` guards**

Keep the existing process-level protection that a `qcx_restore_waiting` client cannot manually send `spawn` or `begin` to bypass the restore wait.

- [ ] **Step 7: Run focused tests**

```bash
cmake --build build
ctest --test-dir build -R '^qcx_restore_(session|routes)$' --output-on-failure
```

- [ ] **Step 8: Commit**

```bash
git add src/qcx/restore_session.c src/qcx/restore_session.h src/sv_user.c \
    tests/qcx/restore_session_test.c tests/qcx/restore_routes_test.cmake
git commit -m "qcx: commit restored identity at begin"
```

**Review gate:** Confirm there is exactly one code path that writes saved role/team/spawn parms into a live `client_t`: `QCX_RestoreSessionCommitBegin()`.

---

### Task 6: Reduce timeout, cancel, and map transitions to unbind-or-drop

**Repository:** `mvdsv`

**Files:**
- Modify: `src/qcx/restore_session.c`
- Modify: `tests/qcx/restore_session_test.c`
- Modify: `tests/integration/qc2cpp_acceptance.py`

**Interfaces:**
- Consumes:
  ```c
  client->qcx_restore_fallback_allowed
  ```
- Produces: rollback-free abandonment/cancellation.

- [ ] **Step 1: Add ACTIVE-survives-cancel regression**

Set up:

```text
Alice -> ACTIVE
Bob -> AVAILABLE
restore session still WAITING
```

Then simulate changed map-transition spawn parms for Alice and cancel the restore session.

Assert:

```text
Alice saved role remains committed
Alice saved team remains committed
Alice new change parms remain intact
no old requested identity reappears
```

This specifically guards the former:

```text
SV_SaveSpawnparms()
-> QCX_RestoreSessionCancel()
-> accidental rollback
```

bug.

- [ ] **Step 2: Add BOUND-cancel regression**

Set up:

```text
Alice -> BOUND, no begin
```

Cancel.

Assert:

```text
client role/team/spawn parms are unchanged from pre-bind values
binding is gone
```

No restore operation is expected because there was never a mutation.

- [ ] **Step 3: Add fallback authorization cases**

For a bound fresh connection:

```text
fallback_allowed == true:
    timeout/continue -> clear binding -> ordinary fresh signon

fallback_allowed == false:
    timeout/continue -> clear binding -> SV_DropClient
```

Carried clients must use ordinary fallback.

- [ ] **Step 4: Simplify `QCX_RestoreSessionFinish(true)`**

For each non-active roster entry:

```text
perform saved-game disconnect semantics when saved.spawned != 0
free abandoned saved edict
mark roster entry abandoned
```

For a live bound connection:

```text
clear pending binding
if fallback_allowed:
    queue ordinary signon
else:
    drop connection
```

Do not write role/team/spawn parms.

- [ ] **Step 5: Simplify `QCX_RestoreSessionReset()`**

Reset must:

```text
clear waiting/pending/roster indexes
clear deadline/session state
clear restore pause
```

It must not mutate active connection identity.

For pending fresh clients, apply the same fallback/drop rule before discarding the session if cancellation is expected to keep connections alive.

- [ ] **Step 6: Run unit and process tests**

```bash
cmake --build build
ctest --test-dir build -R '^qcx_restore_session$' --output-on-failure
```

Run the focused Native process restore acceptance, including:

```text
timeout
qcx_restore_continue
map during partial restore
saved-role/requested-role password asymmetry
```

- [ ] **Step 7: Commit**

```bash
git add src/qcx/restore_session.c tests/qcx/restore_session_test.c \
    tests/integration/qc2cpp_acceptance.py
git commit -m "qcx: discard pending bindings without identity rollback"
```

**Review gate:** Search for any code that attempts to restore a pre-bind role/team/spawn snapshot. There should be none.

---

### Task 7: Run the complete MVDSV restore acceptance matrix

**Repository:** `mvdsv`

**Files:**
- Modify only if tests reveal a real defect.
- Test: `tests/integration/qc2cpp_acceptance.py`

**Interfaces:**
- Validates the implementation produced by Tasks 2-6.

- [ ] **Step 1: Run fast QCX tests**

```bash
cmake --build build
ctest --test-dir build -L qcx --output-on-failure
```

Expected: all QCX fast/contract/unit tests pass.

- [ ] **Step 2: Run QWSP Native acceptance**

Use:

```text
map: e1m2
deathmatch: 0
transport: Native
```

Required scenarios:

```text
immediate connected-player save/load
death -> map restart -> load
e1m2 -> e1m1 -> load
mismatched name -> rename -> restore
timeout -> ordinary new player
qcx_restore_continue -> ordinary new player
conflicting incoming role/team -> saved identity committed at begin
saved spectator restore
two clients whose live physical slots differ from saved slots
manual spawn/begin during wait is ignored
partial restore -> map transition keeps ACTIVE identity and abandons BOUND correctly
saved-role/requested-role asymmetric password cases
```

- [ ] **Step 3: Run the same QWSP matrix with Wasm**

Use:

```text
map: e1m2
deathmatch: 0
transport: Wasm
WASI SDK: /Users/ivan/opt/wasi-sdk-33.0-arm64-macos
```

Expected behavior must match Native.

- [ ] **Step 4: Run cross-transport restore**

Verify:

```text
Native save -> Wasm load
Wasm save -> Native load
```

for the same generated QWSP schema.

- [ ] **Step 5: Commit only test fixes that were necessary**

If no code changes were required, do not create an empty commit.

If acceptance coverage itself was extended:

```bash
git add tests/integration/qc2cpp_acceptance.py
git commit -m "tests: verify deferred roster identity commit"
```

**Review gate:** Do not update qc2cpp's external revision pin until both Native and Wasm acceptance are green.

---

### Task 8: Update the qc2cpp MVDSV integration pin

**Repository:** `qc2cpp`

**Files:**
- Modify: `tests/integration/external-revisions.json`
- Modify: `tests/integration/mvdsv/source-sha256.tsv`

**Interfaces:**
- Consumes: final reviewed and accepted MVDSV commit from Task 7.
- Produces: qc2cpp integration verification against the exact deferred-commit MVDSV implementation.

- [ ] **Step 1: Record the final MVDSV commit**

```bash
git -C <mvdsv-checkout> rev-parse HEAD
```

Use that exact SHA in `external-revisions.json`.

- [ ] **Step 2: Regenerate/update the MVDSV source manifest**

Update hashes only for files actually changed relative to the previous pinned MVDSV revision.

Expected likely paths include:

```text
src/qcx/restore_session.c
src/qcx/restore_session.h
src/server.h
src/sv_main.c
src/sv_user.c
tests/integration/qc2cpp_acceptance.py
tests/qcx/restore_routes_test.cmake
tests/qcx/restore_session_test.c
```

Use the repository's existing manifest-generation/check workflow; do not hand-wave mismatched hashes.

- [ ] **Step 3: Run qc2cpp integration contracts**

Run the existing external revision / source manifest tests plus the normal release/integration tests that consume the pinned MVDSV source.

Expected: qc2cpp verifies the exact MVDSV commit from Task 7.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/external-revisions.json \
    tests/integration/mvdsv/source-sha256.tsv
git commit -m "integration: pin deferred roster restore mvdsv"
```

**Review gate:** Re-fetch both branch HEADs and perform one final cross-repository review of the pin and the MVDSV implementation.

---

## Final Completion Checks

- [ ] `BOUND` does not mutate saved gameplay identity into `client_t`.
- [ ] `Cmd_Begin_f()` is the only commit point.
- [ ] `ACTIVE` identity is never rolled back.
- [ ] Timeout/cancel uses unbind-or-drop, not field rollback.
- [ ] Saved-role authentication and requested-role fallback authentication are independent.
- [ ] `spawn`/`begin` cannot bypass restore waiting.
- [ ] `saved.spawned == 0` runs ordinary gameplay callbacks after identity commit.
- [ ] `saved.spawned == 1` preserves restored gameplay state and skips duplicate callbacks.
- [ ] QCMS V2 and QCX ABI are unchanged.
- [ ] Native and Wasm QWSP acceptance pass on `e1m2`, `deathmatch 0`.
- [ ] Native↔Wasm cross-transport restore passes.
- [ ] qc2cpp pins the exact final MVDSV commit.

The following implementation artifacts must no longer exist:

```text
qcx_restore_has_original_identity
qcx_restore_original_spectator
qcx_restore_original_team
qcx_restore_original_spawn_parms
QCX_RestoreSessionRememberAdmissionIdentity
QCX_RestoreSessionRestoreOriginalIdentity
```

The following architecture remains unchanged:

```text
qcx_restore_waiting
qcx_restore_pending
qcx_restore_roster_index
roster entry state machine
slot reservations
client-slot permutation
QCMS V2
restore pause
fresh-map restore
```
