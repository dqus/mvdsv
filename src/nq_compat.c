#include "qwsvdef.h"
#include "nq_compat.h"

void NQ_ApplyModelBounds(edict_t *entity, const char *model)
{
	/* Inline bounds/relinking belong to the existing setmodel path. */
	if (model[0] == '*') {
		return;
	}
	if (!strcmp(model, "maps/b_explob.bsp")) {
		VectorClear(entity->v->mins);
		VectorSet(entity->v->maxs, 32, 32, 64);
	} else {
		VectorSet(entity->v->mins, -16, -16, -16);
		VectorSet(entity->v->maxs, 16, 16, 16);
	}
	VectorSubtract(entity->v->maxs, entity->v->mins, entity->v->size);
}

qbool NQ_CvarOverride(const char *name, float game_deathmatch, float *out_value)
{
	/* Existing PR1 workaround: these id1 rules must not run in SP/coop. */
	if (game_deathmatch == 0.0f
		&& (!strcmp(name, "timelimit") || !strcmp(name, "samelevel"))) {
		*out_value = 0.0f;
		return true;
	}
	return false;
}

qbool NQ_QueueLocalCommand(const char *text)
{
	if (!strcmp(text, "restart\n")) {
		Cbuf_AddText(va("map %s\n", sv.mapname));
		return true;
	}
	return false;
}
