find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(NAME qc2cpp_profile_tool
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_profile_tests.py")
set_tests_properties(qc2cpp_profile_tool PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    LABELS "fast")

set(qc2cpp_acceptance_inputs
    QC2CPP_COMPILER
    QC2CPP_CHECKER
    QC2CPP_QW_MANIFEST
    QC2CPP_ASSET_DIR)
set(qc2cpp_acceptance_ready TRUE)
foreach(input IN LISTS qc2cpp_acceptance_inputs)
    if("${${input}}" STREQUAL "")
        set(qc2cpp_acceptance_ready FALSE)
    endif()
endforeach()

if(NOT qc2cpp_acceptance_ready)
    add_custom_target(qc2cpp_acceptance_assets
        COMMAND ${CMAKE_COMMAND} -E echo
            "qc2cpp_acceptance_assets requires QC2CPP_COMPILER, QC2CPP_CHECKER,"
            " QC2CPP_QW_MANIFEST and QC2CPP_ASSET_DIR at configure time"
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM)
    return()
endif()

foreach(input IN LISTS qc2cpp_acceptance_inputs)
    if(NOT EXISTS "${${input}}")
        message(FATAL_ERROR "${input} does not exist: ${${input}}")
    endif()
endforeach()

get_filename_component(qc2cpp_compiler "${QC2CPP_COMPILER}" ABSOLUTE)
get_filename_component(qc2cpp_checker "${QC2CPP_CHECKER}" ABSOLUTE)
get_filename_component(qc2cpp_manifest "${QC2CPP_QW_MANIFEST}" ABSOLUTE)
get_filename_component(qc2cpp_assets "${QC2CPP_ASSET_DIR}" ABSOLUTE)
get_filename_component(qc2cpp_manifest_directory "${qc2cpp_manifest}" DIRECTORY)
get_filename_component(qc2cpp_qcc_directory "${qc2cpp_manifest_directory}" DIRECTORY)
get_filename_component(qc2cpp_source_root "${qc2cpp_qcc_directory}" DIRECTORY)
set(qc2cpp_qwsp_enabled FALSE)
if(NOT "${QC2CPP_QWSP_MANIFEST}" STREQUAL "")
    if(NOT EXISTS "${QC2CPP_QWSP_MANIFEST}")
        message(FATAL_ERROR "QC2CPP_QWSP_MANIFEST does not exist: ${QC2CPP_QWSP_MANIFEST}")
    endif()
    get_filename_component(qc2cpp_qwsp_manifest "${QC2CPP_QWSP_MANIFEST}" ABSOLUTE)
    get_filename_component(qc2cpp_qwsp_manifest_directory "${qc2cpp_qwsp_manifest}" DIRECTORY)
    get_filename_component(qc2cpp_qwsp_qcc_directory "${qc2cpp_qwsp_manifest_directory}" DIRECTORY)
    get_filename_component(qc2cpp_qwsp_source_root "${qc2cpp_qwsp_qcc_directory}" DIRECTORY)
    if(NOT qc2cpp_qwsp_source_root STREQUAL qc2cpp_source_root)
        message(FATAL_ERROR
            "QC2CPP_QWSP_MANIFEST and QC2CPP_QW_MANIFEST must belong to the same qc2cpp source tree")
    endif()
    set(qc2cpp_qwsp_enabled TRUE)
endif()

# qc2cpp copies these resources from QC2CPP_SOURCE_ROOT at generation time.  The
# QW manifest is deliberately supplied from that source tree, so make the
# acceptance target track the copied inputs as well as the compiler binary.
# Otherwise an emitter-resource change can leave a stale generated project
# behind even though the acceptance target itself remains up to date.
foreach(required_path IN ITEMS
        "${qc2cpp_source_root}/CMakeLists.txt"
        "${qc2cpp_source_root}/runtime/include/qc"
        "${qc2cpp_source_root}/runtime/include/game"
        "${qc2cpp_source_root}/tools/qc2cpp-check"
        "${qc2cpp_source_root}/cmake/WasmToolchain.cmake")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR
            "QC2CPP_QW_MANIFEST must be qcc/qw-qc/progs.src from a qc2cpp source tree; "
            "missing generated-resource input ${required_path}")
    endif()
endforeach()

file(GLOB_RECURSE qc2cpp_generated_resource_inputs CONFIGURE_DEPENDS
    "${qc2cpp_source_root}/runtime/include/qc/*"
    "${qc2cpp_source_root}/runtime/include/game/*"
    "${qc2cpp_source_root}/tools/qc2cpp-check/*")
list(APPEND qc2cpp_generated_resource_inputs
    "${qc2cpp_source_root}/cmake/WasmToolchain.cmake"
    "${qc2cpp_source_root}/runtime/src/freestanding_memory.cpp"
    "${qc2cpp_source_root}/runtime/src/runtime_heap_native.cpp"
    "${qc2cpp_source_root}/runtime/src/runtime_heap_wasm.cpp"
    "${qc2cpp_source_root}/runtime/third_party/tlsf/include/tlsf.h"
    "${qc2cpp_source_root}/runtime/third_party/tlsf/src/tlsf.c"
    "${qc2cpp_source_root}/runtime/third_party/tlsf/LICENSE")

set(qc2cpp_manifest_sources "${qc2cpp_manifest}")
file(STRINGS "${qc2cpp_manifest}" qc2cpp_manifest_lines)
foreach(line IN LISTS qc2cpp_manifest_lines)
    string(STRIP "${line}" source)
    if(source MATCHES "^[^#].*\\.qc$")
        get_filename_component(source_path "${qc2cpp_manifest_directory}/${source}" ABSOLUTE)
        if(EXISTS "${source_path}")
            list(APPEND qc2cpp_manifest_sources "${source_path}")
        endif()
    endif()
endforeach()

if(qc2cpp_qwsp_enabled)
    set(qc2cpp_qwsp_manifest_sources "${qc2cpp_qwsp_manifest}")
    file(STRINGS "${qc2cpp_qwsp_manifest}" qc2cpp_qwsp_manifest_lines)
    foreach(line IN LISTS qc2cpp_qwsp_manifest_lines)
        string(STRIP "${line}" source)
        if(source MATCHES "^[^#].*\\.qc$")
            get_filename_component(source_path "${qc2cpp_qwsp_manifest_directory}/${source}" ABSOLUTE)
            if(EXISTS "${source_path}")
                list(APPEND qc2cpp_qwsp_manifest_sources "${source_path}")
            endif()
        endif()
    endforeach()
endif()

set(qc2cpp_acceptance_root "${CMAKE_BINARY_DIR}/qc2cpp-acceptance")
set(qc2cpp_generated "${qc2cpp_acceptance_root}/generated")
set(qc2cpp_fatal_manifest "${CMAKE_CURRENT_SOURCE_DIR}/../qcx/fixtures/fatal/progs.src")
set(qc2cpp_fatal_generated "${qc2cpp_acceptance_root}/fatal-generated")
set(qc2cpp_fatal_native_build "${qc2cpp_acceptance_root}/fatal-native")
set(qc2cpp_fatal_native_game "${qc2cpp_fatal_native_build}/game${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(qc2cpp_native_build "${qc2cpp_acceptance_root}/native")
set(qc2cpp_native_game "${qc2cpp_native_build}/game${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(qc2cpp_native_input_ids "${qc2cpp_acceptance_root}/native-input-ids.txt")
if(qc2cpp_qwsp_enabled)
    set(qc2cpp_qwsp_generated "${qc2cpp_acceptance_root}/qwsp-generated")
    set(qc2cpp_qwsp_native_build "${qc2cpp_acceptance_root}/qwsp-native")
    set(qc2cpp_qwsp_native_game "${qc2cpp_qwsp_native_build}/game${CMAKE_SHARED_LIBRARY_SUFFIX}")
    set(qc2cpp_qwsp_native_input_ids "${qc2cpp_acceptance_root}/qwsp-native-input-ids.txt")
endif()

add_custom_command(
    OUTPUT "${qc2cpp_generated}/CMakeLists.txt"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${qc2cpp_acceptance_root}"
    COMMAND "${qc2cpp_compiler}" "${qc2cpp_manifest}" -o "${qc2cpp_generated}"
    DEPENDS "${qc2cpp_compiler}" ${qc2cpp_manifest_sources}
        ${qc2cpp_generated_resource_inputs}
    VERBATIM)
add_custom_target(qc2cpp_acceptance_generated
    DEPENDS "${qc2cpp_generated}/CMakeLists.txt")

add_custom_command(
    OUTPUT "${qc2cpp_native_game}"
    COMMAND ${CMAKE_COMMAND} -S "${qc2cpp_generated}" -B "${qc2cpp_native_build}"
        -G "${CMAKE_GENERATOR}" -DQC2CPP_NATIVE_PLUGIN=ON
    COMMAND ${CMAKE_COMMAND} --build "${qc2cpp_native_build}" --target game
    COMMAND "${qc2cpp_checker}" --compile-commands "${qc2cpp_native_build}"
        --generated-project "${qc2cpp_generated}"
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT=${qc2cpp_native_input_ids}
        -DCOMPILER=${qc2cpp_compiler}
        -DCHECKER=${qc2cpp_checker}
        -DMANIFEST=${qc2cpp_manifest}
        -DWASI_SDK_ROOT=
        -P ${CMAKE_CURRENT_SOURCE_DIR}/write_qc2cpp_input_ids.cmake
    DEPENDS "${qc2cpp_generated}/CMakeLists.txt" "${qc2cpp_checker}"
    VERBATIM)
add_custom_target(qc2cpp_acceptance_native_assets DEPENDS "${qc2cpp_native_game}")
add_dependencies(qc2cpp_acceptance_native_assets mvdsv)

if(qc2cpp_qwsp_enabled)
    add_custom_command(
        OUTPUT "${qc2cpp_qwsp_generated}/CMakeLists.txt"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${qc2cpp_acceptance_root}"
        COMMAND "${qc2cpp_compiler}" "${qc2cpp_qwsp_manifest}" -o "${qc2cpp_qwsp_generated}"
        DEPENDS "${qc2cpp_compiler}" ${qc2cpp_qwsp_manifest_sources}
            ${qc2cpp_generated_resource_inputs}
        VERBATIM)
    add_custom_command(
        OUTPUT "${qc2cpp_qwsp_native_game}"
        COMMAND ${CMAKE_COMMAND} -S "${qc2cpp_qwsp_generated}" -B "${qc2cpp_qwsp_native_build}"
            -G "${CMAKE_GENERATOR}" -DQC2CPP_NATIVE_PLUGIN=ON
        COMMAND ${CMAKE_COMMAND} --build "${qc2cpp_qwsp_native_build}" --target game
        COMMAND "${qc2cpp_checker}" --compile-commands "${qc2cpp_qwsp_native_build}"
            --generated-project "${qc2cpp_qwsp_generated}"
        COMMAND ${CMAKE_COMMAND}
            -DOUTPUT=${qc2cpp_qwsp_native_input_ids}
            -DCOMPILER=${qc2cpp_compiler}
            -DCHECKER=${qc2cpp_checker}
            -DMANIFEST=${qc2cpp_qwsp_manifest}
            -DWASI_SDK_ROOT=
            -P ${CMAKE_CURRENT_SOURCE_DIR}/write_qc2cpp_input_ids.cmake
        DEPENDS "${qc2cpp_qwsp_generated}/CMakeLists.txt" "${qc2cpp_checker}"
        VERBATIM)
    add_custom_target(qc2cpp_qwsp_native_assets DEPENDS "${qc2cpp_qwsp_native_game}")
    add_dependencies(qc2cpp_qwsp_native_assets mvdsv)
endif()

add_custom_command(
    OUTPUT "${qc2cpp_fatal_generated}/CMakeLists.txt"
    COMMAND "${qc2cpp_compiler}" "${qc2cpp_fatal_manifest}" -o "${qc2cpp_fatal_generated}"
    DEPENDS "${qc2cpp_compiler}" "${qc2cpp_fatal_manifest}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../qcx/fixtures/fatal/fatal.qc"
        ${qc2cpp_generated_resource_inputs}
    VERBATIM)
add_custom_command(
    OUTPUT "${qc2cpp_fatal_native_game}"
    COMMAND ${CMAKE_COMMAND} -S "${qc2cpp_fatal_generated}" -B "${qc2cpp_fatal_native_build}"
        -G "${CMAKE_GENERATOR}" -DQC2CPP_NATIVE_PLUGIN=ON
    COMMAND ${CMAKE_COMMAND} --build "${qc2cpp_fatal_native_build}" --target game
    DEPENDS "${qc2cpp_fatal_generated}/CMakeLists.txt"
    VERBATIM)
add_custom_target(qc2cpp_fatal_native_assets DEPENDS "${qc2cpp_fatal_native_game}")
add_dependencies(qc2cpp_fatal_native_assets mvdsv)

set(qc2cpp_acceptance_asset_targets qc2cpp_acceptance_native_assets qc2cpp_fatal_native_assets)
if(qc2cpp_qwsp_enabled)
    list(APPEND qc2cpp_acceptance_asset_targets qc2cpp_qwsp_native_assets)
    add_test(NAME qc2cpp_qwsp_monsters_native
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qwsp_monster_acceptance.py"
            --mode native --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_qwsp_native_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/qwsp-native")
endif()
if(TARGET qc2cpp_optional_fields_native_assets)
    list(APPEND qc2cpp_acceptance_asset_targets qc2cpp_optional_fields_native_assets)
endif()
add_test(NAME qc2cpp_server_map_native
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
        --suite map --mode native --server $<TARGET_FILE:mvdsv>
        --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
        --output "${qc2cpp_acceptance_root}/runs/native"
        --expect-model-presence)
add_test(NAME qc2cpp_legacy_strings_native
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
        --suite map --mode native --server $<TARGET_FILE:mvdsv>
        --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
        --output "${CMAKE_BINARY_DIR}/tests/qcx/legacy-strings-native/run"
        --expect-legacy-strings)
set_tests_properties(qc2cpp_legacy_strings_native PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
if(TARGET qc2cpp_optional_fields_native_assets)
    add_test(NAME qc2cpp_optional_fields_native
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite map --mode native --server $<TARGET_FILE:mvdsv>
            --artifacts "${CMAKE_BINARY_DIR}/tests/qcx/optional-fields-native/build"
            --assets "${qc2cpp_assets}"
            --output "${CMAKE_BINARY_DIR}/tests/qcx/optional-fields-native/run"
            --expect-optional-fields)
    set_tests_properties(qc2cpp_optional_fields_native PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
endif()
add_test(NAME qc2cpp_save_native_native
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
        --suite save --mode native --server $<TARGET_FILE:mvdsv>
        --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
        --output "${qc2cpp_acceptance_root}/runs/save-native-native")
add_test(NAME qc2cpp_fatal_native
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
        --suite fatal --mode native --server $<TARGET_FILE:mvdsv>
        --artifacts "${qc2cpp_fatal_native_build}" --assets "${qc2cpp_assets}"
        --output "${qc2cpp_acceptance_root}/runs/fatal-native")

if(NOT "${QC2CPP_FTE_CLIENT}" STREQUAL "")
    if(NOT EXISTS "${QC2CPP_FTE_CLIENT}")
        message(FATAL_ERROR "QC2CPP_FTE_CLIENT does not exist: ${QC2CPP_FTE_CLIENT}")
    endif()
    get_filename_component(qc2cpp_fte_client "${QC2CPP_FTE_CLIENT}" ABSOLUTE)
    add_test(NAME qc2cpp_client_native
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite client --mode native --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/client-native"
            --client "${qc2cpp_fte_client}")
    add_test(NAME qc2cpp_network_native
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite network --mode native --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/network-native"
            --client "${qc2cpp_fte_client}")
    if(qc2cpp_qwsp_enabled)
        add_test(NAME qc2cpp_qwsp_command_route_native
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
                --suite command-route --mode native --server $<TARGET_FILE:mvdsv>
                --artifacts "${qc2cpp_qwsp_native_build}" --assets "${qc2cpp_assets}"
                --output "${qc2cpp_acceptance_root}/runs/qwsp-command-route-native"
                --client "${qc2cpp_fte_client}")
    endif()
    add_test(NAME qc2cpp_spectator_native
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite spectator --mode native --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/spectator-native"
            --client "${qc2cpp_fte_client}")
    add_test(NAME qc2cpp_save_connected_native
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite save-connected --mode native --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/save-connected-native"
            --client "${qc2cpp_fte_client}")
    add_test(NAME qc2cpp_roster_restore_native
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite roster-restore --mode native --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/roster-restore-native"
            --client "${qc2cpp_fte_client}")
endif()

if(MVDSV_QC2CPP_WASM)
    if("${QC2CPP_WASI_SDK_ROOT}" STREQUAL "" OR NOT EXISTS "${QC2CPP_WASI_SDK_ROOT}")
        message(FATAL_ERROR "MVDSV_QC2CPP_WASM acceptance requires QC2CPP_WASI_SDK_ROOT")
    endif()
    get_filename_component(qc2cpp_wasi_sdk "${QC2CPP_WASI_SDK_ROOT}" ABSOLUTE)
    set(qc2cpp_wasm_build "${qc2cpp_acceptance_root}/wasm")
    set(qc2cpp_wasm_game "${qc2cpp_wasm_build}/game.wasm")
    set(qc2cpp_wasm_input_ids "${qc2cpp_acceptance_root}/wasm-input-ids.txt")
    if(qc2cpp_qwsp_enabled)
        set(qc2cpp_qwsp_wasm_build "${qc2cpp_acceptance_root}/qwsp-wasm")
        set(qc2cpp_qwsp_wasm_game "${qc2cpp_qwsp_wasm_build}/game.wasm")
        set(qc2cpp_qwsp_wasm_input_ids "${qc2cpp_acceptance_root}/qwsp-wasm-input-ids.txt")
    endif()
    set(qc2cpp_fatal_wasm_build "${qc2cpp_acceptance_root}/fatal-wasm")
    set(qc2cpp_fatal_wasm_game "${qc2cpp_fatal_wasm_build}/game.wasm")
    set(qc2cpp_restore_oom_wasm_build "${qc2cpp_acceptance_root}/restore-oom-wasm")
    set(qc2cpp_restore_oom_wasm_game "${qc2cpp_restore_oom_wasm_build}/game.wasm")
    add_custom_command(
        OUTPUT "${qc2cpp_wasm_game}"
        # CMAKE_EXE_LINKER_FLAGS_INIT is consumed only during the first configure.
        # Regenerate this disposable child build so an updated generated toolchain
        # cannot retain an earlier fixed-memory profile in CMakeCache.txt.
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${qc2cpp_wasm_build}"
        COMMAND ${CMAKE_COMMAND} -S "${qc2cpp_generated}" -B "${qc2cpp_wasm_build}"
            -G "${CMAKE_GENERATOR}"
            -DCMAKE_TOOLCHAIN_FILE=${qc2cpp_generated}/cmake/WasmToolchain.cmake
            -DQC_WASI_SDK_ROOT=${qc2cpp_wasi_sdk}
        COMMAND ${CMAKE_COMMAND} --build "${qc2cpp_wasm_build}" --target game
        COMMAND ${CMAKE_COMMAND}
            -DOUTPUT=${qc2cpp_wasm_input_ids}
            -DCOMPILER=${qc2cpp_compiler}
            -DCHECKER=${qc2cpp_checker}
            -DMANIFEST=${qc2cpp_manifest}
            -DWASI_SDK_ROOT=${qc2cpp_wasi_sdk}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/write_qc2cpp_input_ids.cmake
        DEPENDS "${qc2cpp_generated}/CMakeLists.txt"
        VERBATIM)
    add_custom_target(qc2cpp_acceptance_wasm_assets DEPENDS "${qc2cpp_wasm_game}")
    add_dependencies(qc2cpp_acceptance_wasm_assets mvdsv)

    if(qc2cpp_qwsp_enabled)
        add_custom_command(
            OUTPUT "${qc2cpp_qwsp_wasm_game}"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${qc2cpp_qwsp_wasm_build}"
            COMMAND ${CMAKE_COMMAND} -S "${qc2cpp_qwsp_generated}" -B "${qc2cpp_qwsp_wasm_build}"
                -G "${CMAKE_GENERATOR}"
                -DCMAKE_TOOLCHAIN_FILE=${qc2cpp_qwsp_generated}/cmake/WasmToolchain.cmake
                -DQC_WASI_SDK_ROOT=${qc2cpp_wasi_sdk}
            COMMAND ${CMAKE_COMMAND} --build "${qc2cpp_qwsp_wasm_build}" --target game
            COMMAND ${CMAKE_COMMAND}
                -DOUTPUT=${qc2cpp_qwsp_wasm_input_ids}
                -DCOMPILER=${qc2cpp_compiler}
                -DCHECKER=${qc2cpp_checker}
                -DMANIFEST=${qc2cpp_qwsp_manifest}
                -DWASI_SDK_ROOT=${qc2cpp_wasi_sdk}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/write_qc2cpp_input_ids.cmake
            DEPENDS "${qc2cpp_qwsp_generated}/CMakeLists.txt"
            VERBATIM)
        add_custom_target(qc2cpp_qwsp_wasm_assets DEPENDS "${qc2cpp_qwsp_wasm_game}")
        add_dependencies(qc2cpp_qwsp_wasm_assets mvdsv)
    endif()

    add_custom_command(
        OUTPUT "${qc2cpp_fatal_wasm_game}"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${qc2cpp_fatal_wasm_build}"
        COMMAND ${CMAKE_COMMAND} -S "${qc2cpp_fatal_generated}" -B "${qc2cpp_fatal_wasm_build}"
            -G "${CMAKE_GENERATOR}"
            -DCMAKE_TOOLCHAIN_FILE=${qc2cpp_fatal_generated}/cmake/WasmToolchain.cmake
            -DQC_WASI_SDK_ROOT=${qc2cpp_wasi_sdk}
        COMMAND ${CMAKE_COMMAND} --build "${qc2cpp_fatal_wasm_build}" --target game
        DEPENDS "${qc2cpp_fatal_generated}/CMakeLists.txt"
        VERBATIM)
    add_custom_target(qc2cpp_fatal_wasm_assets DEPENDS "${qc2cpp_fatal_wasm_game}")
    add_dependencies(qc2cpp_fatal_wasm_assets mvdsv)
    add_custom_command(
        OUTPUT "${qc2cpp_restore_oom_wasm_game}"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${qc2cpp_restore_oom_wasm_build}"
        COMMAND ${CMAKE_COMMAND} -S "${qc2cpp_fatal_generated}" -B "${qc2cpp_restore_oom_wasm_build}"
            -G "${CMAKE_GENERATOR}"
            -DCMAKE_TOOLCHAIN_FILE=${qc2cpp_fatal_generated}/cmake/WasmToolchain.cmake
            -DQC_WASI_SDK_ROOT=${qc2cpp_wasi_sdk}
            -DQC2CPP_RUNTIME_HEAP_BYTES=32768U
        COMMAND ${CMAKE_COMMAND} --build "${qc2cpp_restore_oom_wasm_build}" --target game
        DEPENDS "${qc2cpp_fatal_generated}/CMakeLists.txt"
        VERBATIM)
    add_custom_target(qc2cpp_restore_oom_wasm_assets DEPENDS "${qc2cpp_restore_oom_wasm_game}")
    add_dependencies(qc2cpp_restore_oom_wasm_assets mvdsv)
    list(APPEND qc2cpp_acceptance_asset_targets qc2cpp_acceptance_wasm_assets)
    if(qc2cpp_qwsp_enabled)
        list(APPEND qc2cpp_acceptance_asset_targets qc2cpp_qwsp_wasm_assets)
        add_test(NAME qc2cpp_qwsp_monsters_wasm
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qwsp_monster_acceptance.py"
                --mode wasm --server $<TARGET_FILE:mvdsv>
                --artifacts "${qc2cpp_qwsp_wasm_build}" --assets "${qc2cpp_assets}"
                --output "${qc2cpp_acceptance_root}/runs/qwsp-wasm")
    endif()
    list(APPEND qc2cpp_acceptance_asset_targets qc2cpp_fatal_wasm_assets)
    list(APPEND qc2cpp_acceptance_asset_targets qc2cpp_restore_oom_wasm_assets)
    if(TARGET qc2cpp_optional_fields_wasm_assets)
        list(APPEND qc2cpp_acceptance_asset_targets qc2cpp_optional_fields_wasm_assets)
    endif()
    add_test(NAME qc2cpp_server_map_wasm
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite map --mode wasm --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/wasm"
            --expect-model-presence)
    add_test(NAME qc2cpp_legacy_strings_wasm
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite map --mode wasm --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
            --output "${CMAKE_BINARY_DIR}/tests/qcx/legacy-strings-wasm/run"
            --expect-legacy-strings)
    set_tests_properties(qc2cpp_legacy_strings_wasm PROPERTIES
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    if(TARGET qc2cpp_optional_fields_wasm_assets)
        add_test(NAME qc2cpp_optional_fields_wasm
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
                --suite map --mode wasm --server $<TARGET_FILE:mvdsv>
                --artifacts "${CMAKE_BINARY_DIR}/tests/qcx/optional-fields-wasm/build"
                --assets "${qc2cpp_assets}"
                --output "${CMAKE_BINARY_DIR}/tests/qcx/optional-fields-wasm/run"
                --expect-optional-fields)
        set_tests_properties(qc2cpp_optional_fields_wasm PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    endif()
    add_test(NAME qc2cpp_save_wasm_wasm
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite save --mode wasm --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/save-wasm-wasm")
    add_test(NAME qc2cpp_fatal_wasm
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite fatal --mode wasm --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_fatal_wasm_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/fatal-wasm")
    add_test(NAME qc2cpp_restore_oom_wasm
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite restore-oom --mode wasm --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_fatal_wasm_build}"
            --destination-artifacts "${qc2cpp_restore_oom_wasm_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/restore-oom-wasm")
    add_test(NAME qc2cpp_save_native_wasm
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite cross-save --mode wasm --source-mode native --destination-mode wasm
            --server $<TARGET_FILE:mvdsv> --artifacts "${qc2cpp_native_build}"
            --destination-artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/save-native-wasm")
    add_test(NAME qc2cpp_save_wasm_native
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite cross-save --mode wasm --source-mode wasm --destination-mode native
            --server $<TARGET_FILE:mvdsv> --artifacts "${qc2cpp_wasm_build}"
            --destination-artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/save-wasm-native")
    if(NOT "${QC2CPP_FTE_CLIENT}" STREQUAL "")
        add_test(NAME qc2cpp_network_wasm
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
                --suite network --mode wasm --server $<TARGET_FILE:mvdsv>
                --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
                --output "${qc2cpp_acceptance_root}/runs/network-wasm"
                --client "${qc2cpp_fte_client}")
        if(qc2cpp_qwsp_enabled)
            add_test(NAME qc2cpp_qwsp_command_route_wasm
                COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
                    --suite command-route --mode wasm --server $<TARGET_FILE:mvdsv>
                    --artifacts "${qc2cpp_qwsp_wasm_build}" --assets "${qc2cpp_assets}"
                    --output "${qc2cpp_acceptance_root}/runs/qwsp-command-route-wasm"
                    --client "${qc2cpp_fte_client}")
        endif()
        add_test(NAME qc2cpp_spectator_wasm
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
                --suite spectator --mode wasm --server $<TARGET_FILE:mvdsv>
                --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
                --output "${qc2cpp_acceptance_root}/runs/spectator-wasm"
                --client "${qc2cpp_fte_client}")
        add_test(NAME qc2cpp_save_connected_wasm
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
                --suite save-connected --mode wasm --server $<TARGET_FILE:mvdsv>
                --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
                --output "${qc2cpp_acceptance_root}/runs/save-connected-wasm"
                --client "${qc2cpp_fte_client}")
        add_test(NAME qc2cpp_roster_restore_wasm
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
                --suite roster-restore --mode wasm --server $<TARGET_FILE:mvdsv>
                --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
                --output "${qc2cpp_acceptance_root}/runs/roster-restore-wasm"
                --client "${qc2cpp_fte_client}")
    endif()
endif()

add_custom_target(qc2cpp_acceptance_assets DEPENDS ${qc2cpp_acceptance_asset_targets})

foreach(test_name IN ITEMS
	qc2cpp_server_map_native
	qc2cpp_legacy_strings_native
	qc2cpp_optional_fields_native
	qc2cpp_save_native_native
	qc2cpp_fatal_native
	qc2cpp_client_native
	qc2cpp_network_native
	qc2cpp_spectator_native
	qc2cpp_save_connected_native
	qc2cpp_roster_restore_native
	qc2cpp_qwsp_monsters_native
	qc2cpp_qwsp_command_route_native
	qc2cpp_server_map_wasm
	qc2cpp_legacy_strings_wasm
	qc2cpp_optional_fields_wasm
	qc2cpp_save_wasm_wasm
	qc2cpp_fatal_wasm
	qc2cpp_restore_oom_wasm
	qc2cpp_save_native_wasm
	qc2cpp_save_wasm_native
	qc2cpp_network_wasm
	qc2cpp_spectator_wasm
	qc2cpp_save_connected_wasm
	qc2cpp_roster_restore_wasm
	qc2cpp_qwsp_monsters_wasm
	qc2cpp_qwsp_command_route_wasm)
	if(TEST ${test_name})
		set_tests_properties(${test_name} PROPERTIES LABELS "qcx;integration;external")
	endif()
endforeach()
