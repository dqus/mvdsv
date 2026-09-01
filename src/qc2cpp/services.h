#ifndef MVDSV_QC2CPP_SERVICES_H
#define MVDSV_QC2CPP_SERVICES_H

#include "game/plugin_api.h"

struct edict_s;

/* Explicit engine operations shared by PR1 builtins and the qc2cpp host. */
void SV_QC_SetOrigin(struct edict_s *entity, const float origin[3]);
void SV_QC_SetSize(struct edict_s *entity, const float mins[3], const float maxs[3]);
int SV_QC_SetModel(struct edict_s *entity, const char *name);
int SV_QC_PrecacheSound(const char *name);
int SV_QC_PrecacheModel(const char *name);
void SV_QC_LightStyle(int style, const char *value);
void SV_QC_MakeStatic(struct edict_s *entity, const char *model_name);
void SV_QC_ChangeLevel(const char *map);

/* Explicit physics operations shared by PR1 builtins and qc2cpp imports. */
struct edict_s *SV_QC_CheckClient(struct edict_s *self);
float SV_QC_WalkMove(struct edict_s *entity, float yaw, float distance);
float SV_QC_DropToFloor(struct edict_s *entity);

/* Task-owned portions of the mandatory V1 host table. */
void QC_BindWorldServices(qc_host_api_v1_t *host);
void QC_BindMovementServices(qc_host_api_v1_t *host);

/* Mandatory services owned by later tasks terminate instead of succeeding. */
void QC_BindUnavailableServices(qc_host_api_v1_t *host);

#endif
