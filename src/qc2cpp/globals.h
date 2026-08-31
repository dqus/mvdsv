#ifndef MVDSV_QC2CPP_GLOBALS_H
#define MVDSV_QC2CPP_GLOBALS_H

#include "game/shared_global_state.h"

qc_shared_global_state_v1_t *QC_Globals(void);
int QC_ConfigureGlobals(float deathmatch, float coop, float teamplay);
void QC_ClearGlobals(void);
int QC_SetMapName(const char *mapname);

#endif
