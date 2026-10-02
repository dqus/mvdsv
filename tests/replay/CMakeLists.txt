foreach(name tape clock_rng)
	add_executable(qcx_replay_${name}_test replay_${name}_test.c
		${PROJECT_SOURCE_DIR}/src/qcx/replay_tape.c
		${PROJECT_SOURCE_DIR}/src/qcx/replay_probe.c)
	set_target_properties(qcx_replay_${name}_test PROPERTIES
		C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
	target_include_directories(qcx_replay_${name}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
	target_compile_options(qcx_replay_${name}_test PRIVATE
		-Wall -Wextra -Wpedantic -Werror -UNDEBUG)
	target_link_libraries(qcx_replay_${name}_test PRIVATE m)
	add_test(NAME qcx_replay_${name} COMMAND qcx_replay_${name}_test)
endforeach()
