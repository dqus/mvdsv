find_package(Python3 REQUIRED COMPONENTS Interpreter)

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
    "${qc2cpp_source_root}/cmake/WasmToolchain.cmake")

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

set(qc2cpp_acceptance_root "${CMAKE_BINARY_DIR}/qc2cpp-acceptance")
set(qc2cpp_generated "${qc2cpp_acceptance_root}/generated")
set(qc2cpp_native_build "${qc2cpp_acceptance_root}/native")
set(qc2cpp_native_game "${qc2cpp_native_build}/game${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(qc2cpp_native_input_ids "${qc2cpp_acceptance_root}/native-input-ids.txt")

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

set(qc2cpp_acceptance_asset_targets qc2cpp_acceptance_native_assets)
add_test(NAME qc2cpp_server_map_native
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
        --suite map --mode native --server $<TARGET_FILE:mvdsv>
        --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
        --output "${qc2cpp_acceptance_root}/runs/native")

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
    add_test(NAME qc2cpp_spectator_native
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite spectator --mode native --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_native_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/spectator-native"
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
    list(APPEND qc2cpp_acceptance_asset_targets qc2cpp_acceptance_wasm_assets)
    add_test(NAME qc2cpp_server_map_wasm
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
            --suite map --mode wasm --server $<TARGET_FILE:mvdsv>
            --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
            --output "${qc2cpp_acceptance_root}/runs/wasm")
    if(NOT "${QC2CPP_FTE_CLIENT}" STREQUAL "")
        add_test(NAME qc2cpp_network_wasm
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
                --suite network --mode wasm --server $<TARGET_FILE:mvdsv>
                --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
                --output "${qc2cpp_acceptance_root}/runs/network-wasm"
                --client "${qc2cpp_fte_client}")
        add_test(NAME qc2cpp_spectator_wasm
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/qc2cpp_acceptance.py"
                --suite spectator --mode wasm --server $<TARGET_FILE:mvdsv>
                --artifacts "${qc2cpp_wasm_build}" --assets "${qc2cpp_assets}"
                --output "${qc2cpp_acceptance_root}/runs/spectator-wasm"
                --client "${qc2cpp_fte_client}")
    endif()
endif()

add_custom_target(qc2cpp_acceptance_assets DEPENDS ${qc2cpp_acceptance_asset_targets})
