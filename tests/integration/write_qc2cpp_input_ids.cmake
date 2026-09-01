foreach(required IN ITEMS OUTPUT COMPILER CHECKER MANIFEST)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "write_qc2cpp_input_ids requires -D${required}=...")
    endif()
endforeach()

file(SHA256 "${COMPILER}" compiler_sha256)
file(SHA256 "${CHECKER}" checker_sha256)
file(SHA256 "${MANIFEST}" manifest_sha256)
file(WRITE "${OUTPUT}"
    "compiler=${COMPILER}\n"
    "compiler_sha256=${compiler_sha256}\n"
    "checker=${CHECKER}\n"
    "checker_sha256=${checker_sha256}\n"
    "manifest=${MANIFEST}\n"
    "manifest_sha256=${manifest_sha256}\n"
    "wasi_sdk_root=${WASI_SDK_ROOT}\n")
