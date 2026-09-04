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
