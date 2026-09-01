file(READ "${MVDSV_SOURCE_DIR}/src/pr2_exec.c" pr2)
foreach(callback IN ITEMS
    QC_DispatchClientConnect
    QC_DispatchPutClientInServer
    QC_DispatchClientDisconnect
    QC_DispatchClientPreThink
    QC_DispatchClientPostThink
    QC_DispatchClientKill
    QC_DispatchClientCommand
    QC_DispatchClientSay
    QC_DispatchSetNewParms
    QC_DispatchSetChangeParms
    QC_DispatchClientUserInfoChanged)
    string(FIND "${pr2}" "${callback}(" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "qc2cpp client route ${callback} is missing from pr2_exec.c")
    endif()
endforeach()
