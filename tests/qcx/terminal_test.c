#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>

#include "qwsvdef.h"

static jmp_buf terminal_exit;
static unsigned int clear_globals_calls;
static unsigned int clear_entities_calls;
static unsigned int clear_legacy_string_borrows_calls;
static unsigned int terminal_unpublish_calls;

void QCX_ClearGlobals(void) { ++clear_globals_calls; }
void QCX_ClearEntities(void) { ++clear_entities_calls; }
void QCX_ClearLegacyStringBorrows(void) { ++clear_legacy_string_borrows_calls; }

void QCX_TestObserverTerminalUnpublish(void)
{
	++terminal_unpublish_calls;
}

void SV_Error(char *error, ...)
{
	(void)error;
	longjmp(terminal_exit, 1);
}

/* This target deliberately compiles the real adapter implementation into the
 * test translation unit.  The terminal path has no C++ frame, so longjmp here
 * observes the otherwise non-returning server boundary without modelling it. */
#include "../../src/qcx/adapter.c"

static void test_fatal_clears_published_views_before_server_exit(void)
{
	qcx_program_diagnostic_v1_t diagnostic = {
		.message = "terminal test", .message_size = 13U};
	qcx_published = true;
	clear_globals_calls = 0U;
	clear_entities_calls = 0U;
	clear_legacy_string_borrows_calls = 0U;
	terminal_unpublish_calls = 0U;
	if (setjmp(terminal_exit) == 0) {
		QCX_Fatal(NULL, &diagnostic);
		assert(!"QCX_Fatal returned");
	}
	assert(clear_globals_calls == 1U);
	assert(clear_entities_calls == 1U);
	assert(clear_legacy_string_borrows_calls == 1U);
	assert(terminal_unpublish_calls == 1U);
	assert(!qcx_published);
}

static void test_fatal_before_publication_does_not_unpublish(void)
{
	qcx_published = false;
	clear_globals_calls = 0U;
	clear_entities_calls = 0U;
	clear_legacy_string_borrows_calls = 0U;
	terminal_unpublish_calls = 0U;
	if (setjmp(terminal_exit) == 0) {
		QCX_Fatal(NULL, NULL);
		assert(!"QCX_Fatal returned");
	}
	assert(clear_globals_calls == 0U);
	assert(clear_entities_calls == 0U);
	assert(clear_legacy_string_borrows_calls == 0U);
	assert(terminal_unpublish_calls == 0U);
}

static void test_unpublish_is_exactly_once_after_publication(void)
{
	qcx_published = true;
	clear_globals_calls = 0U;
	clear_entities_calls = 0U;
	clear_legacy_string_borrows_calls = 0U;
	terminal_unpublish_calls = 0U;

	QCX_Unpublish(NULL);
	QCX_Unpublish(NULL);

	assert(clear_globals_calls == 1U);
	assert(clear_entities_calls == 1U);
	assert(clear_legacy_string_borrows_calls == 1U);
	assert(terminal_unpublish_calls == 1U);
	assert(!qcx_published);
}

int main(void)
{
	test_fatal_clears_published_views_before_server_exit();
	test_fatal_before_publication_does_not_unpublish();
	test_unpublish_is_exactly_once_after_publication();
	return 0;
}
