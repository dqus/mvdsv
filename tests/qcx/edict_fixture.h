#ifndef MVDSV_QCX_TEST_EDICT_FIXTURE_H
#define MVDSV_QCX_TEST_EDICT_FIXTURE_H

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Unit fixtures own their wrapper table independently of the server loader. */
static void QCX_TestFreeEdicts(void)
{
	free(sv.edicts);
	sv.edicts = NULL;
}

/* Reset host state without losing an earlier fixture allocation. */
static void QCX_TestResetServer(size_t capacity)
{
	static int cleanup_registered;
	if (!cleanup_registered) {
		assert(atexit(QCX_TestFreeEdicts) == 0);
		cleanup_registered = 1;
	}
	QCX_TestFreeEdicts();
	memset(&sv, 0, sizeof(sv));
	sv.edicts = calloc(capacity, sizeof(*sv.edicts));
	assert(sv.edicts != NULL);
}

#endif
