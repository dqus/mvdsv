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
