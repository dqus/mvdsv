# QCX Performance Runner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a maintained local MVDSV runner that captures comparable PR1, QCX Native and QCX Wasm server CPU profiles on `povdmm4` without modifying the user's game assets.

**Architecture:** `tools/profile_qcx.py` is a macOS-only developer command. It consumes explicit server, FTE, asset and game-artifact paths; builds an isolated basedir for one selected mode; starts headless clients; records process CPU windows and optional Instruments traces. Its pure configuration and command-building helpers are covered by a standalone Python unit test; no actual game process is added to CTest or CI.

**Tech Stack:** Python 3 standard library, MVDSV integration-process helpers, macOS `proc_pid_rusage`, `xcrun xctrace`, FTEQW.

**Spec:** `docs/superpowers/specs/2026-09-09-qcx-performance-runner-design.md`

## Global Constraints

- The default benchmark map is `povdmm4`.
- The tool is local developer tooling, never a CTest performance gate or CI job.
- Every executable, asset and game-artifact path is explicit; embed no machine-local absolute path.
- The runner creates an empty caller-specified output directory and never writes inside the supplied asset directory.
- PR1 validity must not depend on QCX-only `qc2cpp_test_events`.
- Each run reaps FTE, MVDSV and optional `xctrace` processes on both success and failure.
- Keep raw CPU samples; do not encode a benchmark threshold or winner claim.

---

## File Map

- `tools/profile_qcx.py`: local CLI, disposable basedir construction, process lifecycle, CPU/trace capture and JSON output.
- `tests/integration/qc2cpp_profile_tests.py`: Python unit coverage for CLI defaults, mode-specific setup and trace command construction; no real Quake installation required.
- `tests/integration/CMakeLists.txt`: registers the pure Python test before the optional external qc2cpp acceptance configuration.
- `docs/qc2cpp-adapter.md`: short maintainer invocation example and statement that the runner is not CI.

### Task 1: Define the portable profiling CLI and prove its configuration

**Files:**
- Create: `tools/profile_qcx.py`
- Create: `tests/integration/qc2cpp_profile_tests.py`
- Modify: `tests/integration/CMakeLists.txt`

**Interfaces:**

```python
def parse_arguments(argv: list[str]) -> argparse.Namespace: ...
def mode_program_type(mode: str) -> str: ...
def game_destination(mode: str) -> pathlib.PurePath: ...
def server_command(arguments: argparse.Namespace, basedir: pathlib.Path, port: int) -> list[str]: ...
def client_command(arguments: argparse.Namespace, basedir: pathlib.Path, port: int, index: int) -> list[str]: ...
```

- [ ] **Step 1: Write the failing unit tests for defaults and mode selection**

Create `tests/integration/qc2cpp_profile_tests.py`. Load
`tools/profile_qcx.py` through `importlib.util.spec_from_file_location` and
assert all of the following without executing a server:

```python
arguments = profile.parse_arguments([
    "--mode", "wasm", "--server", "server", "--client", "fte",
    "--assets", "assets", "--game", "game.wasm", "--output", "output",
])
self.assertEqual(arguments.map, "povdmm4")
self.assertEqual(arguments.clients, 2)
self.assertEqual(arguments.client_fps, 77)
self.assertEqual(profile.mode_program_type("pr1"), "0")
self.assertEqual(profile.mode_program_type("native"), "4")
self.assertEqual(profile.mode_program_type("wasm"), "5")
self.assertEqual(profile.game_destination("pr1"), pathlib.PurePath("qw/qwprogs.dat"))
self.assertEqual(profile.game_destination("native").name,
                 "game.dylib" if sys.platform == "darwin" else "game.so")
self.assertEqual(profile.game_destination("wasm"), pathlib.PurePath("qw/game.wasm"))
```

Add command assertions that all server modes select `+sv_progtype`,
`+sv_progsname` (`qwprogs` for PR1, `game` otherwise), `+map povdmm4`, and
that headless clients set `+vid_renderer headless`, `+cl_maxfps 77` before
`+connect`.

- [ ] **Step 2: Run the test and verify RED**

Run:

```bash
python3 tests/integration/qc2cpp_profile_tests.py
```

Expected: failure because `tools/profile_qcx.py` does not yet exist.

- [ ] **Step 3: Add the argument/configuration implementation**

Create `tools/profile_qcx.py` with `argparse` required options:

```text
--mode {pr1,native,wasm}
--server PATH --client PATH --assets PATH --game PATH --output DIR
```

Add optional `--map povdmm4`, `--clients 2`, `--client-fps 77`, `--warmup 5`,
`--duration 60`, `--runs 3`, and `--trace`. Reject non-positive client/count
and duration values. Validate the input executables/files/directories and
reject a pre-existing output directory before spawning anything. Implement
the five interfaces above without module-level machine-specific paths.

For `game_destination("native")`, choose `qw/game.dylib` on Darwin and
`qw/game.so` on Linux; reject other platforms. The tool itself must reject
non-Darwin before attempting CPU measurement, per the macOS-only spec.

- [ ] **Step 4: Register and run the pure test**

At the top of `tests/integration/CMakeLists.txt`, after `find_package`, add:

```cmake
add_test(NAME qc2cpp_profile_tool
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_profile_tests.py")
set_tests_properties(qc2cpp_profile_tool PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
```

Reconfigure and run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
ctest --test-dir build --output-on-failure -R '^qc2cpp_profile_tool$'
```

Expected: one passing, asset-independent Python test.

- [ ] **Step 5: Commit the CLI/configuration boundary**

```bash
git add tools/profile_qcx.py tests/integration/qc2cpp_profile_tests.py tests/integration/CMakeLists.txt
git commit -m "feat: add QCX performance profile CLI"
```

### Task 2: Implement isolated capture lifecycle and summary output

**Files:**
- Modify: `tools/profile_qcx.py`
- Modify: `tests/integration/qc2cpp_profile_tests.py`

**Interfaces:**

```python
def prepare_basedir(arguments: argparse.Namespace, run: pathlib.Path) -> pathlib.Path: ...
def process_cpu_seconds(pid: int) -> float: ...
def trace_command(pid: int, trace: pathlib.Path, seconds: float) -> list[str]: ...
def run_once(arguments: argparse.Namespace, run: pathlib.Path) -> dict[str, object]: ...
```

- [ ] **Step 1: Extend the unit test with isolated-input and trace checks**

Using `tempfile.TemporaryDirectory`, create a fake `assets` directory with
`PAK0.PAK` and `maps/povdmm4.bsp`, and a mode-appropriate fake game input.
Assert `prepare_basedir` creates `run/base/qw`, symlinks the PAK/map source,
copies the mode-selected game file, and never creates files inside `assets`.
Assert a second call that would reuse an existing run directory raises an
explicit error.

Assert `trace_command(42, pathlib.Path("capture.trace"), 60)` equals a command
beginning:

```python
["xcrun", "xctrace", "record", "--template", "Time Profiler"]
```

and contains `--attach`, `"42"`, `--time-limit`, `"60s"`, and `--no-prompt`.

- [ ] **Step 2: Run the extended test and verify RED**

Run:

```bash
python3 tests/integration/qc2cpp_profile_tests.py
```

Expected: failure because the isolated-run and trace helpers are absent.

- [ ] **Step 3: Add disposable setup, CPU measurement and lifecycle handling**

Reuse `available_udp_port`, `prepare_client_directory` and `RunningProcess`
from `tests/integration` by deriving the repository-relative integration path
from `__file__`; do not embed `/Users/...` paths. Implement a dedicated
`prepare_basedir` that creates `run/base/qw`, links recognized PAK files and
the requested map source, and copies the caller's one game artifact to the
mode-specific destination.

Use `proc_pid_rusage` and `mach_timebase_info` through `ctypes` for process
user-plus-system seconds. `run_once` must start MVDSV, then the requested
number of headless FTE clients; wait until the server log contains one
`" entered the game"` entry for every client; warm up; take CPU before and
after the configured window; and reject a dead server or client. It must not
send `qc2cpp_test_events` or require any QCX observer command.

On `--trace`, require `xcrun` before starting the capture, attach xctrace to
the live MVDSV PID, wait for it to finish, and export the Time Profiler table
as `profile.xml`. Without `--trace`, collect only CPU data. Always retain
server/client/xctrace logs under the run directory and terminate/reap every
child in `finally`.

Write `summary.json` after all runs with input paths, resolved command lines,
mode, map, workload controls, macOS timebase and raw per-window values:
`elapsed_seconds`, `process_cpu_seconds`, `cpu_percent`, client count and
disconnect/death status. Do not compute or assert a comparison result.

- [ ] **Step 4: Run the unit test and inspect CLI help**

Run:

```bash
python3 tests/integration/qc2cpp_profile_tests.py
python3 tools/profile_qcx.py --help
ctest --test-dir build --output-on-failure -R '^qc2cpp_profile_tool$'
```

Expected: all checks pass; help identifies `povdmm4` as the default and
explains that `--trace` captures Instruments Time Profiler with Record Waiting
Threads disabled by the user.

- [ ] **Step 5: Commit the runnable capture tool**

```bash
git add tools/profile_qcx.py tests/integration/qc2cpp_profile_tests.py
git commit -m "feat: capture local QCX performance profiles"
```

### Task 3: Document invocation and perform a bounded Wasm smoke capture

**Files:**
- Modify: `docs/qc2cpp-adapter.md`

**Interfaces:**

```text
python3 tools/profile_qcx.py --mode wasm --server <mvdsv> --client <fteqw>
    --assets <id1> --game <game.wasm> --output <new-directory>
```

- [ ] **Step 1: Add a maintainer-facing invocation example**

Add a `Performance profiling` subsection to `docs/qc2cpp-adapter.md` stating
that the tool is a local macOS aid, never CI; its default is `povdmm4`; output
must be a new directory; and it preserves raw JSON/log/optional trace data.
Show the exact interface above and one PR1 substitution using
`--mode pr1 --game <qwprogs.dat>`. State that compare runs must hold artifact
build type, client count, FPS, map and window duration constant.

- [ ] **Step 2: Run a short real Wasm smoke capture on `povdmm4`**

Use the current local MVDSV binary, FTE client, `/Users/ivan/qdata/id1` assets
and current `game.wasm`, but write only under a fresh `mktemp -d` child:

```bash
profile_root=$(mktemp -d /tmp/qcx-profile-smoke.XXXXXX)
python3 tools/profile_qcx.py --mode wasm \
  --server /Users/ivan/git/mvdsv/build/mvdsv \
  --client /Users/ivan/git/fteqw/engine/release/fteqw-macosx-cl \
  --assets /Users/ivan/qdata/id1 \
  --game /Users/ivan/qdata/qcx-wasm/game.wasm \
  --output "$profile_root/run" \
  --runs 1 --warmup 2 --duration 5
```

Expected: `summary.json` names `povdmm4`, contains one CPU sample, and the
input asset directory remains unmodified. This is a smoke test, not a
performance conclusion.

- [ ] **Step 3: Commit docs and smoke acceptance**

```bash
git add docs/qc2cpp-adapter.md
git commit -m "docs: describe QCX performance profiling"
```

### Task 4: Final review and scope check

**Files:** none.

- [ ] **Step 1: Run the complete tool verification set**

```bash
git diff --check
python3 tests/integration/qc2cpp_profile_tests.py
ctest --test-dir build --output-on-failure -R '^(qc2cpp_profile_tool|qc2cpp_server_map_wasm)$'
if git grep -n '/Users/' -- tools/profile_qcx.py tests/integration/qc2cpp_profile_tests.py; then
  exit 1
fi
```

Expected: tests pass and the final `git grep` has no output. The CTest map
acceptance remains a correctness control; it is not treated as benchmark data.

- [ ] **Step 2: Review the implementation against the spec**

Confirm in the diff that `povdmm4` is the default, no CTest/CI performance
gate was added, legacy validity uses no QCX test observer, inputs are explicit,
and all temporary processes are reaped. Do not expand this task into Native or
Wasm performance optimization.
