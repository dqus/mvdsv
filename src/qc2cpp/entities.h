#ifndef MVDSV_QC2CPP_ENTITIES_H
#define MVDSV_QC2CPP_ENTITIES_H

#include "game/plugin_api.h"
#include "game/shared_entity_state.h"

#include <stdint.h>

struct edict_s;

#define QC_INVALID_ENTITY_ID UINT32_MAX

int QC_ConfigureEntities(qc_guest_address_t publication_address);
int QC_BindEntities(void);
void QC_ClearEntities(void);
qc_shared_entity_state_v1_t *QC_Entity(qc_entity_id_t slot);
qc_entity_id_t QC_EdictToSlot(const struct edict_s *edict);
struct edict_s *QC_SlotToEdict(qc_entity_id_t slot);
void QC_ClearEdict(struct edict_s *edict);
int QC_SetEntityString(struct edict_s *edict, const char *field, const char *value);
const char *QC_EntityStringFieldName(const struct edict_s *edict, const void *member);
qc_plugin_status_t QC_CopyEntityString(const struct edict_s *edict, const char *field,
	char *out, uint32_t capacity, uint32_t *required);
qc_plugin_status_t QC_CopyLegacyString(int32_t token, char *out,
	uint32_t capacity, uint32_t *required);

#endif
