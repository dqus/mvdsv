# qc2cpp game adapter

MVDSV loads a qc2cpp-generated QuakeWorld game through either server transport:

| `sv_progtype` | Transport | Artifact |
| --- | --- | --- |
| `4` | `qc2cpp-native` | platform shared library (`game.dylib`, `game.so`, or `game.dll`) |
| `5` | `qc2cpp-wasm` | `game.wasm` |

The ABI and architecture are defined by the [canonical qc2cpp adapter specification](https://github.com/dqus/qc2cpp/blob/main/docs/superpowers/specs/2026-08-31-mvdsv-qc2cpp-adapter-design.md)
and its [QCX boundary correction](https://github.com/dqus/qc2cpp/blob/main/docs/superpowers/specs/2026-09-02-mvdsv-qcx-boundary-correction-design.md).
This is the MVDSV operations guide; it does not redefine either contract.

## Prerequisites

Build and install the qc2cpp host SDK. Native needs only its C host SDK; Wasm
also needs the qc2cpp Wasmtime component, a Wasmtime C API SDK, and the WASI
SDK used to compile the game.

```sh
# In the qc2cpp checkout.
cmake -S . -B build/adapter-sdk -G Ninja \
  -DQC2CPP_ENABLE_WASMTIME_HOST=ON \
  -DWASMTIME_ROOT=/path/to/wasmtime-sdk \
  -DQC2CPP_WASI_SDK_ROOT=/path/to/wasi-sdk \
  -DQC2CPP_CLANG=/path/to/clang++ \
  -DQC2CPP_WASM_TOOLS=/path/to/wasm-ld
cmake --build build/adapter-sdk --target qc2cpp qc2cpp-check -j4
cmake --install build/adapter-sdk --prefix /path/to/qc2cpp-sdk
```

The verified Wasmtime C API SDK is **48.0.0**. Native games do not depend on
Wasmtime. A locally compatible C API SDK is acceptable for development, but an
SDK update is not a release configuration until the full acceptance matrix has
run. Its version is not part of the game ABI, Wasm module format, or QCMS save
format.

Acceptance also needs a QW manifest (normally `qcc/qw-qc/progs.src`), local QW
PAK/maps supplied through `QC2CPP_ASSET_DIR`, and an FTE client only for real
network, spectator, or connected-restore checks.

## Build MVDSV

Native-only deliberately has no Wasmtime dependency:

```sh
cmake -S . -B build/qc2cpp-native -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qc2cpp-sdk \
  -DMVDSV_QC2CPP_NATIVE=ON -DMVDSV_QC2CPP_TESTS=ON \
  -DQC2CPP_COMPILER=/path/to/qc2cpp \
  -DQC2CPP_CHECKER=/path/to/qc2cpp-check \
  -DQC2CPP_QW_MANIFEST=/path/to/qcc/qw-qc/progs.src \
  -DQC2CPP_ASSET_DIR=/path/to/qw-assets
cmake --build build/qc2cpp-native --target mvdsv qc2cpp_acceptance_assets -j4
```

For both transports, add the Wasm host and game toolchain inputs:

```sh
cmake -S . -B build/qc2cpp-wasm -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qc2cpp-sdk \
  -DMVDSV_QC2CPP_NATIVE=ON -DMVDSV_QC2CPP_WASM=ON \
  -DMVDSV_QC2CPP_TESTS=ON \
  -DMVDSV_WASMTIME_ROOT=/path/to/wasmtime-sdk \
  -DQC2CPP_WASI_SDK_ROOT=/path/to/wasi-sdk \
  -DQC2CPP_COMPILER=/path/to/qc2cpp \
  -DQC2CPP_CHECKER=/path/to/qc2cpp-check \
  -DQC2CPP_QW_MANIFEST=/path/to/qcc/qw-qc/progs.src \
  -DQC2CPP_ASSET_DIR=/path/to/qw-assets
cmake --build build/qc2cpp-wasm --target mvdsv qc2cpp_acceptance_assets -j4
```

Set `-DQC2CPP_FTE_CLIENT=/path/to/fteqw-client` only to request network tests.
CMake rejects an explicitly requested missing client. Without it, client-free
map, save, fatal, and restore tests remain registered. Inspect selection with
`ctest --test-dir build/qc2cpp-wasm -N`.

## Generate and deploy a game

```sh
qc2cpp /path/to/qcc/qw-qc/progs.src -o /tmp/qw-generated

# Native game: a separate shared library loaded at server startup.
cmake -S /tmp/qw-generated -B /tmp/qw-native -G Ninja -DQC2CPP_NATIVE_PLUGIN=ON
cmake --build /tmp/qw-native --target game -j4

# Wasm game.
cmake -S /tmp/qw-generated -B /tmp/qw-wasm -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/qw-generated/cmake/WasmToolchain.cmake \
  -DQC_WASI_SDK_ROOT=/path/to/wasi-sdk
cmake --build /tmp/qw-wasm --target game -j4
```

Put the native `game` library or `game.wasm` in the chosen game directory.
`sv_progsname` is the basename without a platform suffix or `.wasm`:

```sh
# game.dylib on macOS, game.so on Linux
build/qc2cpp-wasm/mvdsv -basedir /path/to/base -game qw \
  +sv_progtype 4 +sv_progsname game +map e1m1

build/qc2cpp-wasm/mvdsv -basedir /path/to/base -game qw \
  +sv_progtype 5 +sv_progsname game +map e1m1
```

A selected qc2cpp transport never falls back to the legacy VM. A missing,
incompatible, or invalid artifact is a startup error.

## Saves, failures, and verification

QCMS saves are backend-neutral: native and Wasm restore each other's saves when
logical game, map, entity capacity, and engine-state identity match. QCMS does
not convert legacy QuakeC saves.

Normal map changes cleanly unpublish and release the old instance. A
post-publication guest fatal, Wasm trap, or post-commit restore failure clears
host views once and terminates the server process. It does not resume the guest
caller, rerun guest shutdown, or unload an executing native library/Wasm store.

Run the complete configured acceptance matrix:

```sh
ctest --test-dir build/qc2cpp-wasm -R '^qc2cpp_' --no-tests=error --output-on-failure
```

It covers checker/topology, map changes, native/Wasm and cross-transport saves,
connected restore, enabled FTE client flows, and terminal fatal/restore-OOM
subprocesses. Generated acceptance builds record compiler, checker, manifest,
and WASI SDK identities in `qc2cpp-acceptance/*-input-ids.txt`.

For a Wasmtime update, build qc2cpp with the candidate SDK, install it into a
new prefix, configure both MVDSV modes, and run the full qc2cpp contract/corpus
matrix plus the command above. Record qc2cpp, MVDSV, FTE, SDK, compiler,
generated-artifact, and asset identities in CI/release output before changing
the known-good pin.

## QCX boundary-correction acceptance record

The correction was accepted with qc2cpp
`a08f9ac8dee4e2eb75859b1e1552d0eddc3b925a` and MVDSV
`d3c8856` using the Wasmtime 48.0.0 C API SDK. Wasmtime remains a dependency
only of the Wasm transport; it is not part of the native transport, game ABI,
Wasm module format, or QCMS save format.

The following commands completed successfully from their respective checkouts:

```sh
# qc2cpp
cmake --build build/adapter-sdk -j 8
cmake --install build/adapter-sdk --prefix build/adapter-install --component HostSdk
QC2CPP_ID1_DIR=/Users/ivan/qdata \
  ctest --test-dir build/adapter-sdk -j 10 --output-on-failure \
  -R '^(qc_plugin_api|qc_native_plugin|qc_host_semantics|qc_host_sdk_install|qc_mvdsv_entry_contract|qc_wasmtime_host|draft34_codegen|draft34_reentry_native|draft34_reentry_wasmtime|draft34_generated_wasmtime)$'

# MVDSV native and Wasm adapter routes
cmake -S . -B build/qc2cpp-native -G Ninja
cmake -S . -B build/qc2cpp-wasm -G Ninja
cmake --build build/qc2cpp-native -j 8
cmake --build build/qc2cpp-wasm -j 8
ctest --test-dir build/qc2cpp-native --output-on-failure -R '^qcx_'
ctest --test-dir build/qc2cpp-wasm --output-on-failure -R '^qcx_'

# MVDSV generated-game acceptance, including each QCMS direction.
ctest --test-dir build/qc2cpp-native --output-on-failure \
  -R '^(qc2cpp_server_map_native|qc2cpp_legacy_strings_native|qc2cpp_optional_fields_native|qc2cpp_save_native_native|qc2cpp_fatal_native|qc2cpp_restore_oom_native|qc2cpp_client_native|qc2cpp_network_native|qc2cpp_spectator_native|qc2cpp_save_connected_native)$'
ctest --test-dir build/qc2cpp-wasm --output-on-failure \
  -R '^(qc2cpp_server_map_wasm|qc2cpp_legacy_strings_wasm|qc2cpp_optional_fields_wasm|qc2cpp_save_wasm_wasm|qc2cpp_save_native_wasm|qc2cpp_save_wasm_native|qc2cpp_fatal_wasm|qc2cpp_restore_oom_wasm|qc2cpp_network_wasm|qc2cpp_spectator_wasm|qc2cpp_save_connected_wasm)$'
```

`qc2cpp_server_map_{native,wasm}` also contains the real generated-game
non-zero entity-reference observation; there is deliberately no duplicate
process target for it. `draft34_codegen` verifies the owner-local generated
topology: no `game_declarations.hpp`, no module construction in owner headers,
and no source unit including the aggregate `game_modules.hpp`. The aggregate
remains only the composition boundary, so this correction did not restore a
central declaration registry or giant generated source file.
