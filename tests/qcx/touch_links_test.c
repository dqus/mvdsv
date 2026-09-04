#include <assert.h>
#include <string.h>

#include "qwsvdef.h"

static entvars_t entity_states[3];
static edict_t entities[3];
static globalvars_t globals;
static int touch_call_count;

server_t sv;
globalvars_t *pr_global_struct = &globals;
float *pr_globals = (float *)&globals;

int EDICT_TO_PROG(const edict_t *entity)
{
	for (int slot = 0; slot < 3; ++slot) {
		if (entity == &entities[slot])
			return slot;
	}
	assert(!"unexpected edict");
	return 0;
}

int CM_FindTouchedLeafs(const vec3_t mins, const vec3_t maxs, int leafs[],
	int maxleafs, int headnode, int *topnode)
{
	(void)mins;
	(void)maxs;
	(void)leafs;
	(void)maxleafs;
	(void)headnode;
	(void)topnode;
	return 0;
}

void PR2_EdictTouch(func_t function)
{
	assert(function == 123);
	assert(PR_GLOBAL(self) == 1);
	assert(PR_GLOBAL(other) == 2);
	assert(PR_GLOBAL(time) == 42.0f);
	++touch_call_count;
	PR_GLOBAL(time) = 77.0f;
}

int main(void)
{
	memset(&sv, 0, sizeof(sv));
	memset(&globals, 0, sizeof(globals));
	memset(entity_states, 0, sizeof(entity_states));
	memset(entities, 0, sizeof(entities));
	memset(sv_areanodes, 0, sizeof(sv_areanodes));

	for (int slot = 0; slot < 3; ++slot) {
		entities[slot].v = &entity_states[slot];
		entities[slot].e.entnum = slot;
		entities[slot].e.area.ed = &entities[slot];
	}

	areanode_t *const root = &sv_areanodes[0];
	root->axis = -1;
	root->trigger_edicts.prev = root->trigger_edicts.next =
		&entities[1].e.area;
	root->solid_edicts.prev = root->solid_edicts.next =
		&root->solid_edicts;
	entities[1].e.area.prev = entities[1].e.area.next =
		&root->trigger_edicts;

	entities[1].v->solid = SOLID_TRIGGER;
	entities[1].v->touch = 123;
	entities[1].v->absmin[0] = entities[1].v->absmin[1] =
		entities[1].v->absmin[2] = -2.0f;
	entities[1].v->absmax[0] = entities[1].v->absmax[1] =
		entities[1].v->absmax[2] = 2.0f;

	entities[2].v->solid = SOLID_BBOX;
	entities[2].v->mins[0] = entities[2].v->mins[1] =
		entities[2].v->mins[2] = -1.0f;
	entities[2].v->maxs[0] = entities[2].v->maxs[1] =
		entities[2].v->maxs[2] = 1.0f;

	PR_GLOBAL(self) = 2;
	PR_GLOBAL(other) = 0;
	PR_GLOBAL(time) = 5.0f;
	sv.time = 42.0;
	sv.max_edicts = 3;

	SV_LinkEdict(&entities[2], true);

	assert(touch_call_count == 1);
	assert(PR_GLOBAL(self) == 2);
	assert(PR_GLOBAL(other) == 0);
	assert(PR_GLOBAL(time) == 77.0f);
	return 0;
}
