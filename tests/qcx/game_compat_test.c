#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <setjmp.h>
#include <string.h>
#include "qwsvdef.h"
#include "game_compat.h"
#include "qcx/adapter.h"

server_t sv;
vm_t *sv_vm;
qbool sv_error;
char fs_gamedir[MAX_OSPATH] = "/fixture";
static jmp_buf load_error;
static unsigned close_calls;
static unsigned init_calls;
static uint32_t fixture_profile;
static uint32_t prepared_profile;
static qcx_plugin_status_t fixture_status;
static qcx_game_api_v1_t fixture_game;

struct qcx_transport { int unused; };
static qcx_transport_t fixture_transport;

void SV_Error(char *message, ...)
{
	(void)message;
	longjmp(load_error, 1);
}

void QCX_BindWorldServices(qcx_host_api_v1_t *host) { (void)host; }
void QCX_BindMovementServices(qcx_host_api_v1_t *host) { (void)host; }
void QCX_BindNetworkServices(qcx_host_api_v1_t *host) { (void)host; }
void QCX_ClearGlobals(void) {}
void QCX_ClearEntities(void) {}
qbool QCX_PreparedGameProfileMatches(uint32_t profile)
{ return prepared_profile == 0U || prepared_profile == profile; }

static qcx_guest_address_t fixture_init(void *context, int32_t time, uint32_t seed)
{
	(void)context;
	(void)time;
	(void)seed;
	++init_calls;
	return 0U;
}

qcx_plugin_status_t QCX_TransportOpen(qcx_transport_kind_t kind, const char *directory,
	const char *basename, const qcx_host_api_v1_t *host, qcx_transport_t **out,
	qcx_program_diagnostic_v1_t *diagnostic)
{
	(void)kind;
	(void)directory;
	(void)basename;
	(void)host;
	(void)diagnostic;
	*out = NULL;
	if (fixture_status == QCX_PLUGIN_OK) {
		fixture_game = (qcx_game_api_v1_t){
			.compatibility_profile = fixture_profile, .init = fixture_init};
		*out = &fixture_transport;
	}
	return fixture_status;
}

const qcx_game_api_v1_t *QCX_TransportGame(qcx_transport_t *transport)
{
	return transport == NULL ? NULL : &fixture_game;
}

void QCX_TransportClose(qcx_transport_t *transport)
{
	assert(transport == &fixture_transport);
	++close_calls;
}

static void load_profile(uint32_t profile)
{
	fixture_profile = profile;
	fixture_status = QCX_PLUGIN_OK;
	QCX_LoadProgs(QCX_TRANSPORT_NATIVE);
	assert(QCX_GameCompatibility() == profile);
	assert(init_calls == 0U); /* Metadata must precede gameplay initialization. */
}

static void assert_identity_offsets(void)
{
#ifdef WITH_NQPROGS
	PR_InitPatchTables();
	for (unsigned i = 0U; i < 62U; ++i) {
		assert(pr_globaloffsetpatch[i] == 0);
	}
	for (unsigned i = 0U; i < 106U; ++i) {
		assert(pr_fieldoffsetpatch[i] == (int)i);
	}
#endif
}

int main(void)
{
	globalvars_t globals = {0};
	const globalvars_t original = globals;
	dprograms_t bytecode = {0};
	pr_global_struct = &globals;
	assert(!PR_GameIsNetQuake() && QCX_GameCompatibility() == 0U);
	progs = &bytecode;
	assert(!PR_GameIsNetQuake());
#ifdef WITH_NQPROGS
	pr_nqprogs = true;
	assert(PR_GameIsNetQuake());
	PR_InitPatchTables();
	assert(pr_fieldoffsetpatch[8] == 9);
#endif
	progs = NULL; /* Stale layout detection must not override a loaded backend. */
	load_profile(QCX_GAME_PROFILE_QUAKEWORLD);
	assert(!PR_GameIsNetQuake());
	assert_identity_offsets();
	QCX_UnloadProgs();
	assert(!QCX_Active() && QCX_GameCompatibility() == 0U);
#ifdef WITH_NQPROGS
	load_profile(QCX_GAME_PROFILE_NETQUAKE);
	assert(PR_GameIsNetQuake());
	assert_identity_offsets();
	QCX_UnloadProgs();
#else
	fixture_profile = QCX_GAME_PROFILE_NETQUAKE;
	const unsigned closes_before = close_calls;
	if (setjmp(load_error) == 0) {
		QCX_LoadProgs(QCX_TRANSPORT_NATIVE);
		assert(!"NQ artifact accepted without NQ support");
	}
	assert(close_calls == closes_before + 1U);
	assert(!QCX_Active() && QCX_GameCompatibility() == 0U);
#endif
	/* A real PR2 instance, not sv_progtype, selects QW behavior. */
	sv_vm = (vm_t *)&bytecode; /* Opaque identity only; never dereferenced. */
	assert(!PR_GameIsNetQuake());
	assert_identity_offsets();
	sv_vm = NULL;
	fixture_status = QCX_PLUGIN_UNAVAILABLE;
	if (setjmp(load_error) == 0) {
		QCX_LoadProgs(QCX_TRANSPORT_NATIVE);
		assert(!"failed transport load returned");
	}
	assert(!QCX_Active() && QCX_GameCompatibility() == 0U);
	load_profile(QCX_GAME_PROFILE_QUAKEWORLD);
	QCX_UnloadProgs();
	prepared_profile = QCX_GAME_PROFILE_NETQUAKE;
	fixture_profile = QCX_GAME_PROFILE_QUAKEWORLD;
	const unsigned closes_before_mismatch = close_calls;
	if (setjmp(load_error) == 0) {
		QCX_LoadProgs(QCX_TRANSPORT_NATIVE);
		assert(!"fresh startup accepted artifact conflicting with prepared save");
	}
	assert(close_calls == closes_before_mismatch + 1U);
	assert(QCX_GameCompatibility() == 0U && !QCX_Active());
	assert(prepared_profile == QCX_GAME_PROFILE_NETQUAKE && init_calls == 0U);
	prepared_profile = 0U;
	assert(init_calls == 0U && memcmp(&globals, &original, sizeof(globals)) == 0);
	assert(pr_global_struct == &globals && progs == NULL);
	return 0;
}
