#include <assert.h>
#include <math.h>

#include "qwsvdef.h"

static globalvars_t globals;
globalvars_t *pr_global_struct = &globals;
static edict_t entity;
static int movement_calls;
static qbool movement_result;

qbool SV_movestep(edict_t *target, vec3_t move, qbool relink)
{
	assert(target == &entity);
	assert(relink);
	assert(fabsf(move[0]) < 0.0001f);
	assert(fabsf(move[1] - 12.0f) < 0.0001f);
	assert(move[2] == 0.0f);
	++movement_calls;
	/* Simulate gameplay reentry from movement; PF2_walkmove owns self restore. */
	globals.self = 6;
	globals.other = 7;
	globals.time = 29.0f;
	return movement_result;
}

int main(void)
{
	entvars_t state = {0};
	entity.v = &state;
	state.flags = FL_ONGROUND;
	for (int success = 0; success <= 1; ++success) {
		globals.self = 5;
		globals.other = 2;
		globals.time = 11.0f;
		movement_result = success;
		assert(PF2_walkmove(&entity, 90.0f, 12.0f) == success);
		assert(globals.self == 5);
		assert(globals.other == 7 && globals.time == 29.0f);
	}
	assert(movement_calls == 2);
	state.flags = 0.0f;
	assert(PF2_walkmove(&entity, 90.0f, 12.0f) == 0);
	assert(movement_calls == 2 && globals.self == 5);
	return 0;
}
