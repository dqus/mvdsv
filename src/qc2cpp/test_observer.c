#include "qwsvdef.h"

#include "qc2cpp/test_observer.h"

#include <stdint.h>

typedef struct qc_test_observer_s {
	uint32_t frame_count;
	uint32_t normal_unpublish_count;
	uint32_t legacy_game_entries;
	uint32_t gameplay_during_init;
	uint32_t initializing;
} qc_test_observer_t;

static qc_test_observer_t observer;

static void QC_TestSnapshot_f(void)
{
	Con_Printf("{\"qc2cpp_test_snapshot\":{\"map\":\"%s\",\"frame_count\":%u}}\n",
		sv.mapname, observer.frame_count);
}

static void QC_TestEvents_f(void)
{
	Con_Printf("{\"qc2cpp_test_events\":{\"normal_unpublish_count\":%u,"
		"\"legacy_game_entries\":%u,\"gameplay_during_init\":%u}}\n",
		observer.normal_unpublish_count, observer.legacy_game_entries,
		observer.gameplay_during_init);
}

void QC_TestObserverRegisterCommands(void)
{
	Cmd_AddCommand("qc2cpp_test_snapshot", QC_TestSnapshot_f);
	Cmd_AddCommand("qc2cpp_test_events", QC_TestEvents_f);
}

void QC_TestObserverInitBegin(void)
{
	++observer.initializing;
}

void QC_TestObserverInitEnd(void)
{
	if (observer.initializing != 0U) {
		--observer.initializing;
	}
}

void QC_TestObserverStartFrame(void)
{
	++observer.frame_count;
	if (observer.initializing != 0U) {
		++observer.gameplay_during_init;
	}
}

void QC_TestObserverNormalUnpublish(void)
{
	++observer.normal_unpublish_count;
}

void QC_TestObserverLegacyGameEntry(void)
{
	++observer.legacy_game_entries;
}
