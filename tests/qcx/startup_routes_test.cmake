if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "MVDSV_SOURCE_DIR is required")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_init.c" source)
string(FIND "${source}" "void SV_SpawnServer" begin)
string(FIND "${source}" "#endif // !CLIENTONLY" end)
if(begin EQUAL -1 OR end EQUAL -1 OR end LESS begin)
	message(FATAL_ERROR "could not isolate SV_SpawnServer")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${source}" ${begin} ${length} spawn_server)

foreach(forbidden IN ITEMS
	"QCX_"
	"QCX_ENABLED"
	"qcx/"
	"ED_FindFieldOffset"
	"sv.game_edicts"
	"pr_edict_size"
	"PR_ClearEdict")
	string(FIND "${spawn_server}" "${forbidden}" position)
	if(NOT position EQUAL -1)
		message(FATAL_ERROR "SV_SpawnServer must delegate ${forbidden} to PR2")
	endif()
endforeach()

# Every gameplay-capable host import uses the one test-only lifecycle observer.
# The fixed count covers the complete current host API surface.
set(gameplay_import_observers 0)
foreach(service_source IN ITEMS
	src/qcx/services_world.c
	src/qcx/services_movement.c
	src/qcx/services_network.c)
	file(READ "${MVDSV_SOURCE_DIR}/${service_source}" source)
	string(REGEX MATCHALL "QCX_ObserveGameplayImport\\(context\\)" observed "${source}")
	list(LENGTH observed observed_count)
	math(EXPR gameplay_import_observers "${gameplay_import_observers} + ${observed_count}")
endforeach()
if(NOT gameplay_import_observers EQUAL 43)
	message(FATAL_ERROR
		"every QCX gameplay host import must observe the init lifecycle; found ${gameplay_import_observers}")
endif()
