#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "qwsvdef.h"

int fofs_items2, fofs_maxspeed, fofs_gravity, fofs_movement, fofs_vw_index;
int fofs_hideentity, fofs_trackent, fofs_visibility, fofs_hide_players, fofs_teleported;
server_t sv;

void SV_Error(char *error, ...) { (void)error; assert(!"unexpected SV_Error"); }

#ifndef USE_PR2
static dprograms_t field_program;
static ddef_t field_definitions[] = {
	{.ofs = 1U, .s_name = 1},
	{.ofs = (unsigned short)(offsetof(entvars_t, owner) / sizeof(float)),
		.s_name = 2},
};
static char field_names[][16] = {"", "items2", "hideentity"};

char *PR1_GetString(int index)
{
	assert(index >= 0 && index < 3);
	return field_names[index];
}

static void configure_field_discovery(void)
{
	field_program.numfielddefs =
		(int)(sizeof(field_definitions) / sizeof(field_definitions[0]));
	progs = &field_program;
	pr_fielddefs = field_definitions;
}
#else
int ED2_FindFieldOffset(char *name)
{
	if (strcmp(name, "items2") == 0) return 4;
	if (strcmp(name, "hideentity") == 0)
		return (int)offsetof(entvars_t, owner);
	return 0;
}

static void configure_field_discovery(void) {}
#endif

int main(void)
{
	configure_field_discovery();
	PR1_ResolveOptionalFieldOffsets();
	assert(fofs_items2 == 4 && fofs_hideentity == (int)offsetof(entvars_t, owner));
	assert(fofs_maxspeed == 0 && fofs_gravity == 0 && fofs_movement == 0);
	assert(fofs_vw_index == 0 && fofs_trackent == 0 && fofs_visibility == 0);
	assert(fofs_hide_players == 0 && fofs_teleported == 0);
	PR_ResetOptionalFieldOffsets();
	assert(fofs_items2 == 0 && fofs_maxspeed == 0 && fofs_gravity == 0 && fofs_movement == 0);
	assert(fofs_vw_index == 0 && fofs_hideentity == 0 && fofs_trackent == 0);
	assert(fofs_visibility == 0 && fofs_hide_players == 0 && fofs_teleported == 0);
	PR1_ResolveOptionalFieldOffsets();
	assert(fofs_hideentity == (int)offsetof(entvars_t, owner));
	static byte storage[8][sizeof(entvars_t)];
	memset(&sv, 0, sizeof(sv));
	pr_edict_size = (int)sizeof(entvars_t);
	sv.game_edicts = (entvars_t *)storage;
	sv.max_edicts = 8;
	for (int slot = 0; slot < sv.max_edicts; ++slot) {
		sv.edicts[slot].v = (entvars_t *)storage[slot];
	}
	((eval_t *)((byte *)sv.edicts[3].v + fofs_hideentity))->_int = 4 * pr_edict_size;
	assert(PR_EntityFieldToEdict(&sv.edicts[3], fofs_hideentity) == &sv.edicts[4]);
	return 0;
}
