#include "qwsvdef.h"

#include "qc2cpp/adapter.h"
#include "qc2cpp/entities.h"
#include "qc2cpp/entries.h"

static qc_entity_id_t QC_EntrySlot(const edict_t *entity, const char *entry)
{
	const qc_entity_id_t slot = QC_EdictToSlot(entity);
	if (slot == QC_INVALID_ENTITY_ID) {
		SV_Error("qc2cpp %s received an invalid edict", entry);
	}
	return slot;
}

void QC_DispatchEdictTouch(edict_t *touched, edict_t *toucher, float time,
	float frametime)
{
	QC_EdictTouch(QC_EntrySlot(touched, "touch"), QC_EntrySlot(toucher, "touch"),
		time, frametime);
}

void QC_DispatchEdictThink(edict_t *thinking, float thinktime, float frametime)
{
	QC_EdictThink(QC_EntrySlot(thinking, "think"), thinktime, frametime);
}

void QC_DispatchEdictBlocked(edict_t *pusher, edict_t *obstacle, float time,
	float frametime)
{
	QC_EdictBlocked(QC_EntrySlot(pusher, "blocked"),
		QC_EntrySlot(obstacle, "blocked"), time, frametime);
}
