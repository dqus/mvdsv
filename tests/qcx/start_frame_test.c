#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

#include "qwsvdef.h"
#include "qcx/adapter.h"

static globalvars_t globals;
static unsigned start_frame_calls;
static float observed_time;
static float observed_frametime;
static qbool observed_bot_frame;

server_t sv;
globalvars_t *pr_global_struct = &globals;
float *pr_globals = (float *)&globals;
vm_t *sv_vm;

void PR_ExecuteProgram(func_t function)
{
	(void)function;
	assert(!"legacy StartFrame selected in active QCX test");
}

intptr_t QDECL VM_Call(vm_t *vm, int nargs, int callnum, ...)
{
	(void)vm;
	(void)nargs;
	(void)callnum;
	assert(!"PR2 VM StartFrame selected in active QCX test");
	return 0;
}

qbool QCX_Active(void)
{
	return true;
}

void QCX_StartFrame(float time, float frametime, qbool is_bot_frame)
{
	++start_frame_calls;
	observed_time = time;
	observed_frametime = frametime;
	observed_bot_frame = is_bot_frame;
}

int main(void)
{
	sv.time = 42.0;
	PR_GLOBAL(frametime) = 0.05f;

	PR2_GameStartFrame(false);
	assert(start_frame_calls == 1U);
	assert(observed_time == 42.0f);
	assert(observed_frametime == 0.05f);
	assert(!observed_bot_frame);

	/* SV_RunBots is a bot-only phase, not a second QW StartFrame. */
	PR2_GameStartFrame(true);
	assert(start_frame_calls == 1U);
	return 0;
}
