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

# Gameplay sites set canonical globals and enter the existing PR2 seam.  They
# must not select QCX or marshal callback arguments themselves: string and
# startup ownership are intentionally covered by later tasks.
foreach(server_source IN ITEMS src/sv_phys.c src/sv_world.c src/sv_user.c)
	file(READ "${MVDSV_SOURCE_DIR}/${server_source}" source)
	if(source MATCHES "QCX_Dispatch")
		message(FATAL_ERROR "${server_source}: gameplay dispatch leaked past PR2")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_save.c" save_source)
if(save_source MATCHES "QCX_")
	message(FATAL_ERROR "src/sv_save.c: QCX save selection leaked past PR2")
endif()

# The legacy call expressions deliberately remain at the server sites.  PR2
# selects QCX using canonical self/other/time/frametime, so it never evaluates
# their scalar func_t in QCX mode.
require_count("src/sv_phys.c" "PR_EdictTouch" 2)
require_count("src/sv_phys.c" "PR_EdictThink" 2)
require_count("src/sv_phys.c" "PR_EdictBlocked" 1)
require_count("src/sv_world.c" "PR_EdictTouch" 1)
require_count("src/sv_user.c" "PR_EdictTouch" 1)
require_count("src/sv_user.c" "PR_EdictBlocked" 1)

# PR2 is the only place that owns typed QCX dispatch for these entries.
require_count("src/pr2_exec.c" "QCX_DispatchEdictTouch" 1)
require_count("src/pr2_exec.c" "QCX_DispatchEdictThink" 1)
require_count("src/pr2_exec.c" "QCX_DispatchEdictBlocked" 1)
