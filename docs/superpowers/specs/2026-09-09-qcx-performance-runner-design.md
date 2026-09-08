# QCX Performance Runner Design

## Goal

Provide one maintained local runner for repeatable MVDSV performance captures
of legacy PR1, QCX Native and QCX Wasm games.  It replaces the disposable
`/tmp` scripts used during the string-path investigation without turning a
developer benchmark into CI.

## Ownership and scope

The runner belongs in `mvdsv/tools/`: MVDSV owns server startup, FTE client
orchestration and the disposable Quake basedir.  `qc2cpp/tools/build_qw_qcx.sh`
continues to build Native/Wasm game artifacts; the runner consumes paths to
already-built artifacts and never builds qc2cpp itself.

The tool is a local macOS developer tool.  It supports Instruments through
`xcrun xctrace` when requested, and it measures process CPU time through the
macOS `proc_pid_rusage` API.  It is intentionally outside CTest and CI because
it requires real Quake assets, an FTE client and a developer's profiling
environment.

## Workload

`povdmm4` is the default map.  Its small, consistently mutually visible player
layout makes the steady-state server work insensitive to player spawn position.
The runner starts a configurable number of headless FTE clients (default two),
uses a fixed configurable client FPS, waits for each client to appear in the
server log, warms up, then measures one or more fixed-duration windows.

The tool must not write into the supplied asset directory.  Each run creates a
disposable basedir, symlinks/copies only inputs into it, chooses a free UDP
port, preserves server/client logs, and always terminates/reaps its processes.
The caller supplies a new output directory; the runner refuses to reuse one.

## Modes and inputs

The CLI accepts an explicit mode and only the inputs that mode needs:

- `pr1`: a `qwprogs.dat` path, started with `sv_progtype 0` and matching
  `sv_progsname`;
- `native`: a `game.dylib`/`game.so` path, started with `sv_progtype 4`;
- `wasm`: a `game.wasm` path, started with `sv_progtype 5`.

All modes require explicit paths to the MVDSV server executable, FTE client and
asset directory.  Defaults may be provided only for workload controls such as
map, client count, client FPS, warm-up duration, window duration and run count;
no machine-specific absolute executable or asset path is embedded in the tool.

The runner records its complete command lines, input paths, mode, map, timing
settings, per-window elapsed time, process CPU time and CPU percentage in a
`summary.json`.  With `--trace`, it also creates one Time Profiler trace and
exports its time-profile XML per run.  `--trace` fails clearly when `xctrace`
is unavailable; CPU-only captures remain valid without it.

## Validity and diagnostics

A measurement is accepted only while the server and every client stay alive,
and after the server log confirms that the configured number of clients entered
the game.  The runner records zero disconnects only when a QCX observer is
available, but it must not require `qc2cpp_test_events`: that command is a
QCX-only test observer and cannot validate a legacy PR1 run.

The runner reports raw samples rather than declaring a winner.  Before/after
or cross-mode conclusions require matching build options, runner settings and
populated client windows.  Time Profiler captures must use Instruments with
Record Waiting Threads disabled; this is recorded in the tool's help and
output metadata but cannot be inferred from a trace reliably.

## Non-goals

- No benchmark threshold or pass/fail performance assertion.
- No CTest target, CI job or automated hardware comparison.
- No game compilation, asset download or mutation of the user's qdata.
- No dependence on QCX test hooks for the PR1 control path.
