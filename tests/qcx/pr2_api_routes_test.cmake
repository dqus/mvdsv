if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "MVDSV_SOURCE_DIR is required")
endif()

# PR2 accepts API version 16 and later, so pre-15 compatibility is dead code.
# Reusable PF2 operations can also serve QCX and must not inspect PR2 VM
# metadata.
file(READ "${MVDSV_SOURCE_DIR}/src/pr2_cmds.c" pr2_cmds)
string(REGEX MATCHALL "gamedata\\.APIversion < 15" api_version_checks "${pr2_cmds}")
list(LENGTH api_version_checks api_version_check_count)
if(NOT api_version_check_count EQUAL 0)
	message(FATAL_ERROR
		"obsolete pre-15 API-version checks remain: ${api_version_check_count}")
endif()
