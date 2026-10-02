foreach(name tape clock_rng capture)
	add_executable(qcx_replay_${name}_test replay_${name}_test.c
		${PROJECT_SOURCE_DIR}/src/qcx/replay_tape.c
		${PROJECT_SOURCE_DIR}/src/qcx/replay_probe.c
		${PROJECT_SOURCE_DIR}/src/qcx/replay_capture.c)
	set_target_properties(qcx_replay_${name}_test PROPERTIES
		C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
	target_include_directories(qcx_replay_${name}_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
	target_compile_options(qcx_replay_${name}_test PRIVATE
		-Wall -Wextra -Wpedantic -Werror -UNDEBUG)
	target_link_libraries(qcx_replay_${name}_test PRIVATE m)
	add_test(NAME qcx_replay_${name} COMMAND qcx_replay_${name}_test)
endforeach()

add_executable(qcx_replay_execution_test replay_execution_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/replay_execution.c
	${PROJECT_SOURCE_DIR}/src/qcx/replay_tape.c
	${PROJECT_SOURCE_DIR}/src/qcx/replay_probe.c)
target_include_directories(qcx_replay_execution_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(qcx_replay_execution_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_link_libraries(qcx_replay_execution_test PRIVATE m)
add_test(NAME qcx_replay_execution COMMAND qcx_replay_execution_test)

add_executable(qcx_replay_checkpoint_test replay_checkpoint_test.c
	${PROJECT_SOURCE_DIR}/src/qcx/replay_checkpoint.c)
target_include_directories(qcx_replay_checkpoint_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_options(qcx_replay_checkpoint_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME qcx_replay_checkpoint COMMAND qcx_replay_checkpoint_test)
