#include <assert.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

#include "qwsvdef.h"

server_t sv;

static jmp_buf error_jump;

void SV_Error(char *error, ...)
{
	(void)error;
	longjmp(error_jump, 1);
}

static void expect_invalid_reference(int reference)
{
	if (setjmp(error_jump) == 0) {
		(void)PROG_TO_EDICT(reference);
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

	assert(EDICT_TO_PROG(NULL) == 0);
	assert(EDICT_TO_PROG(&sv.edicts[5]) == 5 * pr_edict_size);
	assert(PROG_TO_EDICT(0) == &sv.edicts[0]);
	assert(PROG_TO_EDICT(6 * pr_edict_size) == &sv.edicts[6]);
	/* hideentity is an optional field.  Any discovered legacy field offset
	 * has this byte-offset representation; owner provides the fixture slot. */
	const int hideentity_field_offset = (int)offsetof(entvars_t, owner);
	((eval_t *)((byte *)sv.edicts[3].v + hideentity_field_offset))->_int =
		4 * pr_edict_size;
	assert(PR_EntityFieldToEdict(&sv.edicts[3], hideentity_field_offset)
		== &sv.edicts[4]);

	expect_invalid_reference(pr_edict_size - 1);
	expect_invalid_reference(8 * pr_edict_size);
	return 0;
}
