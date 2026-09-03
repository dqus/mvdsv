/*
 * Selected program entity-reference boundary.
 *
 * PR1 stores entity values as byte offsets from sv.game_edicts.  QCX uses
 * slots and is selected in pr2_exec.c; keeping the legacy primitive small
 * and standalone makes the incompatible representations explicit.
 */

#include "qwsvdef.h"

int PR1_EntityReference(const edict_t *entity)
{
	return entity == NULL ? 0 : EDICT_TO_PROG((edict_t *)entity);
}

edict_t *PR1_EntityFromReference(int reference)
{
	if (reference == 0) {
		return &sv.edicts[0];
	}
	if (pr_edict_size <= 0 || reference < 0 || reference % pr_edict_size != 0) {
		SV_Error("invalid legacy entity reference %d", reference);
		return &sv.edicts[0];
	}
	const int slot = reference / pr_edict_size;
	if (slot < 0 || slot >= sv.max_edicts) {
		SV_Error("legacy entity reference %d is outside %d edicts", reference,
			sv.max_edicts);
		return &sv.edicts[0];
	}
	return &sv.edicts[slot];
}

edict_t *PR1_EntityFieldToEdict(const edict_t *owner, int field_offset)
{
	if (owner == NULL || owner->v == NULL || field_offset < 0) {
		SV_Error("invalid legacy entity field reference");
		return &sv.edicts[0];
	}
	const int reference = ((const eval_t *)((const byte *)owner->v + field_offset))->_int;
	return PR1_EntityFromReference(reference);
}
