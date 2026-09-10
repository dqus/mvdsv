if(NOT DEFINED MVDSV_SOURCE_DIR)
	message(FATAL_ERROR "MVDSV_SOURCE_DIR is required")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/server.h" server_header)
if(server_header MATCHES "cs_loadzombie")
	message(FATAL_ERROR "restored QCX clients must not add cs_loadzombie")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_user.c" sv_user)
foreach(required IN ITEMS
	"QCX_RestoreSessionClientWaiting(sv_client)"
	"QCX_RestoreSessionPrepareSpawn(sv_client)"
	"QCX_RestoreSessionBegin(sv_client)"
	"QCX_RestoreSessionClientRoleLocked(sv_client)"
	"QCX_RestoreSessionNameChanged(sv_client)"
	"qcx_restore_list")
	string(FIND "${sv_user}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/sv_user.c: missing restored-client route ${required}")
	endif()
endforeach()

string(FIND "${sv_user}" "{\"qcx_restore_list\", Cmd_RestoreList_f, false}" position)
if(position EQUAL -1)
	message(FATAL_ERROR "qcx_restore_list must be a non-overrideable engine command")
endif()

string(FIND "${sv_user}" "static void Cmd_Spawn_f (void)" spawn_begin)
string(FIND "${sv_user}" "static void SV_SpawnSpectator (void)" spawn_end)
if(spawn_begin EQUAL -1 OR spawn_end EQUAL -1 OR NOT spawn_begin LESS spawn_end)
	message(FATAL_ERROR "could not isolate Cmd_Spawn_f")
endif()
math(EXPR spawn_length "${spawn_end} - ${spawn_begin}")
string(SUBSTRING "${sv_user}" ${spawn_begin} ${spawn_length} spawn_source)
string(FIND "${spawn_source}" "QCX_RestoreSessionPrepareSpawn(sv_client)" prepare_spawn)
string(FIND "${spawn_source}" "else if (sv.loadgame)" legacy_load)
if(prepare_spawn EQUAL -1 OR legacy_load EQUAL -1 OR NOT prepare_spawn LESS legacy_load)
	message(FATAL_ERROR
		"Cmd_Spawn_f must choose the per-client QCX restored path before legacy sv.loadgame")
endif()

string(FIND "${sv_user}" "static void Cmd_Begin_f (void)" begin_begin)
string(FIND "${sv_user}" "//=============================================================================" begin_end)
if(begin_begin EQUAL -1 OR begin_end EQUAL -1 OR NOT begin_begin LESS begin_end)
	message(FATAL_ERROR "could not isolate Cmd_Begin_f")
endif()
math(EXPR begin_length "${begin_end} - ${begin_begin}")
string(SUBSTRING "${sv_user}" ${begin_begin} ${begin_length} begin_source)
foreach(required IN ITEMS
	"restoring_qcx_client = QCX_RestoreSessionClientPending(sv_client)"
	"if (!restoring_qcx_client && !sv.loadgame)"
	"if (sv.loadgame || restoring_qcx_client)")
	string(FIND "${begin_source}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "Cmd_Begin_f must use per-client restored state: ${required}")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_main.c" sv_main)
foreach(required IN ITEMS
	"QCX_RestoreSessionAdmissionRole("
	"QCX_RestoreSessionClientDropped(drop)"
	"!qcx_restore_identity")
	string(FIND "${sv_main}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/sv_main.c: missing restored-client admission/drop route ${required}")
	endif()
endforeach()

string(FIND "${sv_main}" "QCX_RestoreSessionAdmissionRole(" admission_role)
string(FIND "${sv_main}" "CheckPasswords( userinfo" check_passwords)
if(admission_role EQUAL -1 OR check_passwords EQUAL -1 OR NOT admission_role LESS check_passwords)
	message(FATAL_ERROR
		"SVC_DirectConnect must select the saved role before authenticating the connection")
endif()

foreach(required IN ITEMS
	"SV_FindReconnectingClient(net_from, qport)"
	"!strcmp(requested_spectator, \"0\")")
	string(FIND "${sv_main}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "SVC_DirectConnect is missing saved-role authentication edge case ${required}")
	endif()
endforeach()
