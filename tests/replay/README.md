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

MEASURE requires a successful validation schedule bound to the tape, game,
logical inventory and server binary. The schedule supplies omitted libc RNG
draws and validated final state/work evidence. This evidence is not recomputed
inside measurement: counts are cumulative and include the untimed prefix.
Only one fixed FRAME_BEGIN..FRAME_END segment is timed, once per fresh world,
using CLOCK_PROCESS_CPUTIME_ID. Detailed counters and logical checkpoints are
off, and ordinary console/file printing is suppressed during that segment
(fatal errors remain visible). No live outer server loop, network input,
timeouts, outbound sends or RTX run during measurement. Common probe overhead
remains: event/clock dispatch, omitted RNG advancement, inactive observer
branches, real gameplay packet formatting and output-buffer cleanup.

qc2cpp tools/experiments/qcx_command_replay.py records SHA256 provenance,
checks retained artifacts, drives capture/validate/sequential pairs with a
per-arm watchdog, and saves raw JSON/logs. analyze_qcx_command_replay.py refuses
unequal input/state/work/segment evidence before calculating CPU deltas.
