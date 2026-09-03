#include <assert.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

#include "qwsvdef.h"

server_t sv;
int pr_edict_size;

static jmp_buf error_jump;

void SV_Error(char *error, ...)
{
	(void)error;
	longjmp(error_jump, 1);
}

static void expect_invalid_reference(int reference)
{
	if (setjmp(error_jump) == 0) {
		(void)PR1_EntityFromReference(reference);
		assert(!"invalid legacy entity reference did not fail");
	}
}

int main(void)
{
	static byte entity_storage[8][sizeof(entvars_t)];

	memset(&sv, 0, sizeof(sv));
	pr_edict_size = (int)sizeof(entvars_t);
	sv.game_edicts = (entvars_t *)entity_storage;
	sv.max_edicts = 8;
	for (int slot = 0; slot < sv.max_edicts; ++slot) {
		sv.edicts[slot].v = (entvars_t *)entity_storage[slot];
	}

	assert(PR1_EntityReference(NULL) == 0);
	assert(PR1_EntityReference(&sv.edicts[5]) == 5 * pr_edict_size);
	assert(PR1_EntityFromReference(0) == &sv.edicts[0]);
	assert(PR1_EntityFromReference(6 * pr_edict_size) == &sv.edicts[6]);
	/* hideentity is an optional field.  Any discovered legacy field offset
	 * has this byte-offset representation; owner provides the fixture slot. */
	const int hideentity_field_offset = (int)offsetof(entvars_t, owner);
	((eval_t *)((byte *)sv.edicts[3].v + hideentity_field_offset))->_int =
		4 * pr_edict_size;
	assert(PR1_EntityFieldToEdict(&sv.edicts[3], hideentity_field_offset)
		== &sv.edicts[4]);

	expect_invalid_reference(pr_edict_size - 1);
	expect_invalid_reference(8 * pr_edict_size);
	return 0;
}
