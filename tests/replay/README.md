# Native QCX command-replay diagnostic

This default-off probe is not a supported server mode or a performance fix.
Capture and replay a fresh QWSP `povdmm4`, DM4 world, with speedcheck, antilag
and minping disabled. Replay executes the real command groups and physics,
including the otherwise callback-free bot-frame bookkeeping. Output events
retain damage/fixangle consumption without sending packets.

Validation decodes the existing QCGD named logical codec: all durable globals
and all active entities (plus world), including string bytes and symbolic
function/field names. A diagnostic field inventory is derived from retained
generated schemas; unknown declarations fail closed. There is no ABI change.
Shared transient globals (self/other/world, time/frametime, direction and trace
values), engine edict allocation/free/lifetime cells, physics clocks and
relevant client movement/spawn/userinfo values are included separately.
Network signon substates, sockets, packet statistics and client localtime are
not gameplay state in this fixed, antilag-disabled workload. Userinfo key order
is not semantic. Private guest RNG state is not exported: fixed initialization
and repeated later state/work agreement are evidence, not full-state proof.
Other omitted guest-only transient values are constant-empty `string_null`
and shotgun output scratch `blood_org`/`puff_org`. FireBullets assigns puff_org
before use; TraceAttack assigns blood_org before incremented blood_count is
consumed. ClearMultiDamage resets counts before the next shot. These are not
future physics inputs; this gate does not claim to compare emitted packets.

Checkpoints cover map initialization, every ten outer frames and the final
frame. FNV64 hashes are diagnostic, not cryptographic proofs; individual cell
hashes can be logged with `-qcx-probe-detail`. Fifteen cumulative counters cover
outer frames, command groups, top-level commands, physics attempts, StartFrame,
Pre/PostThink, think/touch/blocked, connect/put, allocations/frees, SetNewParms.
Empty frames remain present; recursive >50ms halves remain one recorded command.

`tests/integration/qcx_replay_process.py` runs real UDP two-client capture,
capture-to-replay validation, changed-movement rejection and truncated-input
rejection. Its `qcx_replay_fields.py` helper writes `-qcx-probe-fields` input
from the retained generated tree beside the game artifact. Raw logs/tapes are
ignored external build artifacts, not committed captures.
