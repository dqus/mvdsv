if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "MVDSV_SOURCE_DIR is required")
endif()

function(require_count relative_path needle expected)
	file(READ "${MVDSV_SOURCE_DIR}/${relative_path}" source)
	string(REGEX MATCHALL "${needle}" matches "${source}")
	list(LENGTH matches actual)
	if(NOT actual EQUAL expected)
		message(FATAL_ERROR "${relative_path}: expected ${expected} occurrences of ${needle}, found ${actual}")
	endif()
endfunction()

# Every listed engine site must route qc2cpp through a typed edict boundary.
require_count("src/sv_phys.c" "QC_DispatchEdictTouch" 2)
require_count("src/sv_phys.c" "QC_DispatchEdictThink" 2)
require_count("src/sv_phys.c" "QC_DispatchEdictBlocked" 1)
require_count("src/sv_world.c" "QC_DispatchEdictTouch" 1)
require_count("src/sv_user.c" "QC_DispatchEdictTouch" 1)
require_count("src/sv_user.c" "QC_DispatchEdictBlocked" 1)

# The PR2 fallback must preserve the same boundary if a future call site uses it.
require_count("src/pr2_exec.c" "QC_DispatchEdictTouch" 1)
require_count("src/pr2_exec.c" "QC_DispatchEdictThink" 1)
require_count("src/pr2_exec.c" "QC_DispatchEdictBlocked" 1)
