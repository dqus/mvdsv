file(READ "${MVDSV_SOURCE_DIR}/src/pr2_exec.c" pr2)
foreach(callback IN ITEMS
    QCX_DispatchClientConnect
    QCX_DispatchPutClientInServer
    QCX_DispatchClientDisconnect
    QCX_DispatchClientPreThink
    QCX_DispatchClientPostThink
    QCX_DispatchClientKill
    QCX_DispatchClientCommand
    QCX_DispatchClientSay
    QCX_DispatchSetNewParms
    QCX_DispatchSetChangeParms
    QCX_DispatchClientUserInfoChanged)
    string(FIND "${pr2}" "${callback}(" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "qc2cpp client route ${callback} is missing from pr2_exec.c")
    endif()
endforeach()

# PR2 must not resolve the hidden msg_entity global unless the destination is
# MSG_ONE. Non-client message routes historically ignore that global entirely.
file(READ "${MVDSV_SOURCE_DIR}/src/pr2_cmds.c" pr2_cmds)
string(FIND "${pr2_cmds}" "static edict_t *PR2_WriteMessageEntity(int destination)" helper_begin)
if(helper_begin EQUAL -1)
	message(FATAL_ERROR "PR2 write dispatch must select msg_entity lazily")
endif()
string(SUBSTRING "${pr2_cmds}" ${helper_begin} -1 helper_tail)
string(FIND "${helper_tail}" "\n}\n" helper_end)
if(helper_end EQUAL -1)
	message(FATAL_ERROR "could not isolate PR2_WriteMessageEntity")
endif()
string(SUBSTRING "${helper_tail}" 0 ${helper_end} helper)
string(FIND "${helper}" "if (destination != MSG_ONE)" non_one_check)
string(FIND "${helper}" "return NULL;" null_return)
string(FIND "${helper}" "PROG_TO_EDICT(pr_global_struct->msg_entity)" resolve_entity)
if(non_one_check EQUAL -1 OR null_return EQUAL -1 OR resolve_entity EQUAL -1
		OR NOT non_one_check LESS null_return OR NOT null_return LESS resolve_entity)
	message(FATAL_ERROR
		"PR2 non-MSG_ONE writes must return NULL before resolving msg_entity")
endif()

foreach(write_case IN ITEMS
	G_WRITEBYTE
	G_WRITECHAR
	G_WRITESHORT
	G_WRITELONG
	G_WRITEANGLE
	G_WRITECOORD
	G_WRITESTRING
	G_WRITEENTITY)
	string(FIND "${pr2_cmds}" "case ${write_case}:" case_begin)
	if(case_begin EQUAL -1)
		message(FATAL_ERROR "missing PR2 ${write_case} dispatch")
	endif()
	string(SUBSTRING "${pr2_cmds}" ${case_begin} -1 case_tail)
	string(FIND "${case_tail}" "return 0;" case_end)
	if(case_end EQUAL -1)
		message(FATAL_ERROR "could not isolate PR2 ${write_case} dispatch")
	endif()
	string(SUBSTRING "${case_tail}" 0 ${case_end} case_source)
	string(FIND "${case_source}" "PR2_WriteMessageEntity(args[1])" route)
	if(route EQUAL -1)
		message(FATAL_ERROR "PR2 ${write_case} must route msg_entity lazily")
	endif()
endforeach()

file(READ "${MVDSV_SOURCE_DIR}/src/sv_user.c" sv_user)
string(FIND "${sv_user}" "static void Cmd_Observe_f (void)" observe_begin)
if(observe_begin EQUAL -1)
    message(FATAL_ERROR "could not locate Cmd_Observe_f")
endif()

string(SUBSTRING "${sv_user}" ${observe_begin} -1 observe_tail)
string(FIND "${observe_tail}" "\n}\n" observe_end)
if(observe_end EQUAL -1)
    message(FATAL_ERROR "could not isolate Cmd_Observe_f")
endif()

string(SUBSTRING "${observe_tail}" 0 ${observe_end} observe_source)
string(FIND "${observe_source}" "PR_GameSetNewParms();" new_parms)
string(FIND "${observe_source}"
    "sv_client->spawn_parms[i] = (&PR_GLOBAL(parm1))[i];" copy_parms)
string(FIND "${observe_source}" "SV_SpawnSpectator ();" spawn_spectator)

if(new_parms EQUAL -1 OR copy_parms EQUAL -1 OR spawn_spectator EQUAL -1)
    message(FATAL_ERROR
        "Cmd_Observe_f must preserve the spectator spawn-parameter handoff")
endif()

if(NOT new_parms LESS copy_parms OR NOT copy_parms LESS spawn_spectator)
    message(FATAL_ERROR
        "Cmd_Observe_f must copy GameSetNewParms results before SV_SpawnSpectator")
endif()
