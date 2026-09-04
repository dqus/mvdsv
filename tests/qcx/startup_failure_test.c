#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>

#include "qwsvdef.h"
#include "qcx/adapter.h"

static jmp_buf error_jump;
static qbool expecting_error;

typedef enum failure_stage_e {
	FAIL_NONE,
	FAIL_BIND_ENTITIES,
	FAIL_CONFIGURE_GLOBALS,
	FAIL_OPTIONAL_FIELDS
} failure_stage_t;

typedef enum event_e {
	EVENT_BIND_ENTITIES,
	EVENT_CONFIGURE_GLOBALS,
	EVENT_RESET_OPTIONAL_FIELDS,
	EVENT_RESOLVE_OPTIONAL_FIELDS,
	EVENT_UNPUBLISH,
	EVENT_ERROR
} event_t;

static failure_stage_t failure_stage;
static event_t events[8];
static unsigned event_count;

server_t sv;
server_static_t svs;
cvar_t deathmatch;
cvar_t coop;
cvar_t teamplay;

static void record_event(event_t event)
{
	assert(event_count < sizeof(events) / sizeof(events[0]));
	events[event_count++] = event;
}

qbool QCX_Active(void)
{
	return true;
}

int QCX_BindEntities(void)
{
	record_event(EVENT_BIND_ENTITIES);
	return failure_stage != FAIL_BIND_ENTITIES;
}

int QCX_ConfigureGlobals(float deathmatch_value, float coop_value, float teamplay_value)
{
	(void)deathmatch_value;
	(void)coop_value;
	(void)teamplay_value;
	record_event(EVENT_CONFIGURE_GLOBALS);
	return failure_stage != FAIL_CONFIGURE_GLOBALS;
}

void PR_ResetOptionalFieldOffsets(void)
{
	record_event(EVENT_RESET_OPTIONAL_FIELDS);
}

int QCX_ResolveOptionalEntityFields(void)
{
	record_event(EVENT_RESOLVE_OPTIONAL_FIELDS);
	return failure_stage != FAIL_OPTIONAL_FIELDS;
}

void QCX_Unpublish(void *context)
{
	(void)context;
	record_event(EVENT_UNPUBLISH);
}

void PR1_BindServerState(void)
{
	assert(!"PR1 binding selected in active QCX test");
}

void SV_Error(char *error, ...)
{
	(void)error;
	record_event(EVENT_ERROR);
	if (!expecting_error) {
		assert(!"unexpected SV_Error");
	}
	longjmp(error_jump, 1);
}

static void reset_fixture(failure_stage_t stage)
{
	failure_stage = stage;
	event_count = 0U;
	expecting_error = false;
}

static void expect_failure(failure_stage_t stage, const event_t *expected,
	unsigned expected_count)
{
	reset_fixture(stage);
	expecting_error = true;
	if (setjmp(error_jump) == 0) {
		PR2_BindServerState();
		assert(!"PR2_BindServerState returned after fatal startup failure");
	}
	expecting_error = false;
	assert(event_count == expected_count);
	for (unsigned i = 0U; i < expected_count; ++i) {
		assert(events[i] == expected[i]);
	}
}

int main(void)
{
	const event_t bind_failure[] = {
		EVENT_BIND_ENTITIES,
		EVENT_UNPUBLISH,
		EVENT_ERROR,
	};
	expect_failure(FAIL_BIND_ENTITIES, bind_failure,
		sizeof(bind_failure) / sizeof(bind_failure[0]));

	const event_t globals_failure[] = {
		EVENT_BIND_ENTITIES,
		EVENT_CONFIGURE_GLOBALS,
		EVENT_UNPUBLISH,
		EVENT_ERROR,
	};
	expect_failure(FAIL_CONFIGURE_GLOBALS, globals_failure,
		sizeof(globals_failure) / sizeof(globals_failure[0]));

	const event_t optional_failure[] = {
		EVENT_BIND_ENTITIES,
		EVENT_CONFIGURE_GLOBALS,
		EVENT_RESET_OPTIONAL_FIELDS,
		EVENT_RESOLVE_OPTIONAL_FIELDS,
		EVENT_UNPUBLISH,
		EVENT_ERROR,
	};
	expect_failure(FAIL_OPTIONAL_FIELDS, optional_failure,
		sizeof(optional_failure) / sizeof(optional_failure[0]));

	reset_fixture(FAIL_NONE);
	PR2_BindServerState();
	const event_t success[] = {
		EVENT_BIND_ENTITIES,
		EVENT_CONFIGURE_GLOBALS,
		EVENT_RESET_OPTIONAL_FIELDS,
		EVENT_RESOLVE_OPTIONAL_FIELDS,
	};
	assert(event_count == sizeof(success) / sizeof(success[0]));
	for (unsigned i = 0U; i < event_count; ++i) {
		assert(events[i] == success[i]);
	}
	return 0;
}
