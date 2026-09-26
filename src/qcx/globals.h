#ifndef MVDSV_QC2CPP_GLOBALS_H
#define MVDSV_QC2CPP_GLOBALS_H

#include "qwsvdef.h"
#include "game/shared_global_state.h"

globalvars_t *QCX_Globals(void);
int QCX_ConfigureGlobals(float deathmatch, float coop, float teamplay);
void QCX_ClearGlobals(void);
int QCX_SetMapName(const char *mapname);

#endif
