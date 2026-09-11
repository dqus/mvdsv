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

string(FIND "${sv_user}" "if (sv.paused && !pending_qcx_client && !waiting_qcx_client)" position)
if(position EQUAL -1)
	message(FATAL_ERROR "Cmd_Begin must defer pause notification for every QCX restore handshake")
endif()

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
string(FIND "${spawn_source}" "if (QCX_RestoreSessionClientWaiting(sv_client)) return;" spawn_wait_guard)
string(FIND "${spawn_source}" "if (sv_client->state != cs_connected)" spawn_state_check)
if(spawn_wait_guard EQUAL -1 OR spawn_state_check EQUAL -1 OR NOT spawn_wait_guard LESS spawn_state_check)
	message(FATAL_ERROR "Cmd_Spawn_f must reject waiting QCX clients before state changes")
endif()

string(FIND "${sv_user}" "static void Cmd_Begin_f (void)" begin_begin)
string(FIND "${sv_user}" "//=============================================================================" begin_end)
if(begin_begin EQUAL -1 OR begin_end EQUAL -1 OR NOT begin_begin LESS begin_end)
	message(FATAL_ERROR "could not isolate Cmd_Begin_f")
endif()
math(EXPR begin_length "${begin_end} - ${begin_begin}")
string(SUBSTRING "${sv_user}" ${begin_begin} ${begin_length} begin_source)
foreach(required IN ITEMS
	"pending_qcx_client = QCX_RestoreSessionClientPending(sv_client)"
	"&& QCX_RestoreSessionClientRestoresGameplay(sv_client)"
	"waiting_qcx_client = QCX_RestoreSessionClientWaiting(sv_client)"
	"if (!restoring_qcx_client && !waiting_qcx_client && !sv.loadgame)"
	"if (sv.loadgame || restoring_qcx_client)"
	"if (pending_qcx_client && !QCX_RestoreSessionBegin(sv_client))")
	string(FIND "${begin_source}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "Cmd_Begin_f must use per-client restored state: ${required}")
	endif()
endforeach()
string(FIND "${begin_source}" "if (QCX_RestoreSessionClientWaiting(sv_client)) return;" begin_wait_guard)
string(FIND "${begin_source}" "if (sv_client->state == cs_spawned)" begin_state_check)
if(begin_wait_guard EQUAL -1 OR begin_state_check EQUAL -1 OR NOT begin_wait_guard LESS begin_state_check)
	message(FATAL_ERROR "Cmd_Begin_f must reject waiting QCX clients before state changes")
endif()

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
	"!strcmp(requested_spectator, \"0\")"
	"QCX_RestoreSessionRememberAdmissionIdentity(newcl,")
	string(FIND "${sv_main}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "SVC_DirectConnect is missing saved-role authentication edge case ${required}")
	endif()
endforeach()

string(FIND "${sv_main}" "qcx_requested_spectator =" requested_role)
string(FIND "${sv_main}" "Info_SetValueForKey(userinfo, \"spectator\", \"1\"" forced_role)
string(FIND "${sv_main}" "PR_GameSetNewParms();" new_parms)
string(FIND "${sv_main}" "QCX_RestoreSessionRememberAdmissionIdentity(newcl," remember_identity)
if(requested_role EQUAL -1 OR forced_role EQUAL -1 OR new_parms EQUAL -1
	OR remember_identity EQUAL -1 OR NOT requested_role LESS forced_role
	OR NOT new_parms LESS remember_identity)
	message(FATAL_ERROR
		"SVC_DirectConnect must preserve requested identity before forcing saved role")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/pr2_exec.c" pr2_exec)
string(FIND "${pr2_exec}" "pr2_save_result_t PR2_LoadGame(" load_begin)
if(load_begin EQUAL -1)
	message(FATAL_ERROR "could not locate PR2_LoadGame")
endif()
string(SUBSTRING "${pr2_exec}" ${load_begin} -1 load_tail)
string(FIND "${load_tail}" "\n}\n" load_end)
if(load_end EQUAL -1)
	message(FATAL_ERROR "could not isolate PR2_LoadGame")
endif()
string(SUBSTRING "${load_tail}" 0 ${load_end} pr2_load)
foreach(forbidden IN ITEMS "QCX_LoadGame(" "PR2_SAVE_COMPLETE")
	string(FIND "${pr2_load}" "${forbidden}" position)
	if(NOT position EQUAL -1)
		message(FATAL_ERROR "PR2_LoadGame must not retain the in-place QCX route: ${forbidden}")
	endif()
endforeach()
string(FIND "${pr2_load}" "QCX_Active()" position)
if(NOT position EQUAL -1)
	message(FATAL_ERROR "PR2_LoadGame must prepare a QCMS load before QCX is active")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/qcx/save.c" save_source)
foreach(forbidden IN ITEMS
	"qcx_connected_snapshot"
	"QCX_SaveFingerprint"
	"QCX_LoadGame(")
	string(FIND "${save_source}" "${forbidden}" position)
	if(NOT position EQUAL -1)
		message(FATAL_ERROR "src/qcx/save.c must remove the connected-snapshot path: ${forbidden}")
	endif()
endforeach()
foreach(required IN ITEMS
	"QCX_RestoreSessionBlocksSave()"
	"QCX_HasPreparedLoadGame()"
	"QCX_RestoreSessionInstall(qcx_prepared_image, Sys_DoubleTime())")
	string(FIND "${save_source}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/qcx/save.c missing fresh-restore route ${required}")
	endif()
endforeach()

foreach(required IN ITEMS
	"QCMS V1 saves are unsupported"
	"QCX_SaveIsV1Image")
	string(FIND "${save_source}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/qcx/save.c missing QCMS V1 diagnostic: ${required}")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_save.c" sv_save)
string(FIND "${sv_save}" "SV_SpawnServer(mapname, false, NULL, false, true)" fresh_spawn)
if(fresh_spawn EQUAL -1)
	message(FATAL_ERROR "SV_LoadGame_f must fresh-spawn every prepared QCMS save")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_init.c" sv_init)
foreach(required IN ITEMS
	"QCX_RestoreSessionCancel();"
	"if (!restoring_qc2cpp) QCX_DiscardPreparedLoadGame();"
	"preserved_pause_reasons"
	"SV_PAUSE_MANUAL"
	"qc2cpp restore could not load saved map")
	string(FIND "${sv_init}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "SV_SpawnServer missing fresh-restore lifecycle route: ${required}")
	endif()
endforeach()
string(FIND "${sv_init}" "memset (&sv, 0, sizeof(sv));" server_wipe)
string(FIND "${sv_init}" "sv.paused = preserved_pause_reasons;" restored_manual_pause)
if(server_wipe EQUAL -1 OR restored_manual_pause EQUAL -1
	OR NOT server_wipe LESS restored_manual_pause)
	message(FATAL_ERROR
		"SV_SpawnServer must preserve manual pause after wiping the per-level server state")
endif()

string(FIND "${begin_source}" "QCX_RestoreSessionBegin(sv_client)" restored_begin)
string(FIND "${begin_source}" "if (pending_qcx_client && (sv.paused & SV_PAUSE_MANUAL))" restored_pause)
if(restored_begin EQUAL -1 OR restored_pause EQUAL -1 OR NOT restored_begin LESS restored_pause)
	message(FATAL_ERROR
		"Cmd_Begin_f must send a surviving pause only after the restored client completes begin")
endif()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_main.c" sv_main_lifecycle)
foreach(required IN ITEMS
	"#include \"qcx/save.h\""
	"QCX_DiscardPreparedLoadGame();"
	"SV_PAUSE_MANUAL")
	string(FIND "${sv_main_lifecycle}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/sv_main.c missing restore lifecycle or pause route: ${required}")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_user.c" sv_user_pause)
foreach(required IN ITEMS "void SV_SetPauseReason" "SV_PAUSE_MANUAL")
	string(FIND "${sv_user_pause}" "${required}" position)
	if(position EQUAL -1)
		message(FATAL_ERROR "src/sv_user.c missing pause-reason route: ${required}")
	endif()
endforeach()
