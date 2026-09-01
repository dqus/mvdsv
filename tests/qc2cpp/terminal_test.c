#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>

#include "qwsvdef.h"

static jmp_buf terminal_exit;
static unsigned int clear_globals_calls;
static unsigned int clear_entities_calls;

void QC_ClearGlobals(void) { ++clear_globals_calls; }
void QC_ClearEntities(void) { ++clear_entities_calls; }

void SV_Error(char *error, ...)
{
	(void)error;
	longjmp(terminal_exit, 1);
}

/* This target deliberately compiles the real adapter implementation into the
 * test translation unit.  The terminal path has no C++ frame, so longjmp here
 * observes the otherwise non-returning server boundary without modelling it. */
#include "../../src/qc2cpp/adapter.c"

static void test_fatal_clears_published_views_before_server_exit(void)
{
	qc_program_diagnostic_v1_t diagnostic = {
		.message = "terminal test", .message_size = 13U};
	qc_published = true;
	clear_globals_calls = 0U;
	clear_entities_calls = 0U;
	if (setjmp(terminal_exit) == 0) {
		QC_Fatal(NULL, &diagnostic);
		assert(!"QC_Fatal returned");
	}
	assert(clear_globals_calls == 1U);
	assert(clear_entities_calls == 1U);
	assert(!qc_published);
}

static void test_fatal_before_publication_does_not_unpublish(void)
{
	qc_published = false;
	clear_globals_calls = 0U;
	clear_entities_calls = 0U;
	if (setjmp(terminal_exit) == 0) {
		QC_Fatal(NULL, NULL);
		assert(!"QC_Fatal returned");
	}
	assert(clear_globals_calls == 0U);
	assert(clear_entities_calls == 0U);
}

int main(void)
{
	test_fatal_clears_published_views_before_server_exit();
	test_fatal_before_publication_does_not_unpublish();
	return 0;
}
