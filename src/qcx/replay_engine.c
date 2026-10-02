#include "qwsvdef.h"
#include "qcx/replay_engine.h"
#include "qcx/adapter.h"

static int started, ready, frame_open, group_open, finishing, outer_active;
static unsigned clients;
static double last_begin, warmup;
static char asset_identity[65];

static qcx_replay_clock_t Clock(void)
{
	return (qcx_replay_clock_t){sv.time, realtime, curtime};
}

static void Record(qcx_replay_kind_t kind, uint32_t slot, const void *payload, size_t size,
	const usercmd_t *command)
{
	if (!started || QCX_ReplayMode() != QCX_REPLAY_RECORD) {
		return;
	}
	qcx_replay_event_t event = {
		.kind = kind, .slot = slot, .clock = Clock(), .rng_draws = QCX_ReplayRngDraws(),
		.payload = (uint8_t *)payload, .payload_size = (uint32_t)size
	};
	if (command) {
		event.command.msec = command->msec;
		event.command.buttons = command->buttons;
		event.command.impulse = command->impulse;
		VectorCopy(command->angles, event.command.angles);
		event.command.forwardmove = command->forwardmove;
		event.command.sidemove = command->sidemove;
		event.command.upmove = command->upmove;
	}
	QCX_ReplayRecordEvent(&event);
	if (QCX_ReplayCaptureError()[0]) {
		SV_Error("QCX capture: %s", QCX_ReplayCaptureError());
	}
}

void SV_QCXReplayUnsupported(const char *reason)
{
	if (ready && QCX_ReplayMode() == QCX_REPLAY_RECORD) {
		SV_Error("QCX capture unsupported: %s", reason);
	}
}

void SV_QCXReplayClientCommand(const char *name)
{
	const char *allowed[] = {"new", "pext", "modellist", "soundlist", "prespawn",
		"spawn", "begin", "setinfo", "download", "nextdl", "stopdownload",
		"pings", "serverinfo", NULL};
	for (int i = 0; allowed[i]; ++i) {
		if (!strcmp(name, allowed[i])) {
			return;
		}
	}
	SV_QCXReplayUnsupported("uncaptured client string command");
}

void SV_QCXReplayMapStart(const char *map, int restoring)
{
	if (QCX_ReplayMode() != QCX_REPLAY_RECORD) {
		return;
	}
	if (started || restoring || strcmp(map, "povdmm4") || Cvar_Value("sv_progtype") != 4
		|| Cvar_Value("deathmatch") != 4 || Cvar_Value("sv_speedcheck") != 0
		|| Cvar_Value("sv_antilag") != 0 || Cvar_Value("sv_minping") != 0) {
		SV_Error("QCX capture requires fresh Native povdmm4 DM4, speedcheck/antilag/minping 0");
	}
	qcx_replay_tape_t header = {
		.guest_seed = 0xadc0de, .host_seed = 1, .clients = 32, .asset_hash = "pending"
	};
	strlcpy(header.map, map, sizeof(header.map));
	/* QWSP also reads serverinfo rather than cvars. These optional policies
	 * are deliberately excluded from this fixed diagnostic workload. */
	const char *info_keys[] = {"axe", "dq", "dr", "vwep", "*cheats", "povdmm4", NULL};
	for (int i = 0; info_keys[i]; ++i) {
		if (*Info_ValueForKey(svs.info, info_keys[i]) || *Info_Get(&_localinfo_, info_keys[i])) {
			SV_Error("QCX capture unsupported serverinfo key: %s", info_keys[i]);
		}
	}
	/* Numeric inputs to QWSP and MVDSV physics. The runner records full launch
 * provenance separately; admission/network-only cvars are not simulation. */
	const char *names[] = {
		"deathmatch", "sv_speedcheck", "sv_antilag", "sv_minping", "coop", "skill",
		"registered", "pr_checkextension", "samelevel", "timelimit", "fraglimit", "teamplay",
		"sv_mintic", "sv_maxtic", "sv_maxspeed", "sv_gravity", "sv_accelerate",
		"sv_airaccelerate", "sv_wateraccelerate", "sv_waterfriction", "sv_friction",
		"sv_stopspeed", "sv_maxvelocity", "sv_safestrafe",
		"sv_minpitch", "sv_maxpitch", "pm_ktjump", "pm_slidefix", "pm_airstep", "pm_pground",
		"pm_rampjump", "pm_bunnyspeedcap", "sv_bigcoords", "sv_extlimits", "sv_loadentfiles",
		"maxclients", "maxspectators", "sv_cheats", NULL
	};
	for (int i = 0; names[i]; ++i) {
		size_t offset = strlen(header.config);
		int n = snprintf(header.config + offset, sizeof(header.config) - offset,
			"%s=%.9g;", names[i], (double)Cvar_Value(names[i]));
		if (n < 0 || (size_t)n >= sizeof(header.config) - offset) {
			SV_Error("QCX capture cvar configuration too large");
		}
	}
	if (!QCX_ReplayCaptureStart(&header)) {
		SV_Error("QCX capture could not start");
	}
	started = 1;
	Record(QCX_REPLAY_MAP, QCX_REPLAY_NO_SLOT, NULL, 0, NULL);
}

void SV_QCXReplayMapReady(const char *entities)
{
	if (!started || QCX_ReplayMode() != QCX_REPLAY_RECORD) {
		return;
	}
	if (!QCX_Active()) {
		SV_Error("QCX capture did not load a QCX game");
	}
	snprintf(asset_identity, sizeof(asset_identity), "%08x-%04x",
		(unsigned)sv.map_checksum, (unsigned)CRC_Block((byte *)entities, (int)strlen(entities)));
	QCX_ReplayCaptureIdentity(32, asset_identity);
	ready = 1;
	/* Map startup occurs inside an outer frame. Its normal settling physics
 * stays in MAP; record the remaining packet/physics phase of this frame. */
	if (outer_active) {
		SV_QCXReplayFrameBegin();
	}
}

void SV_QCXReplayFrameBegin(void)
{
	outer_active = 1;
	if (ready && QCX_ReplayMode() == QCX_REPLAY_RECORD) {
		if (frame_open) {
			SV_Error("QCX capture nested frame");
		}
		frame_open = 1;
		Record(QCX_REPLAY_FRAME_BEGIN, QCX_REPLAY_NO_SLOT, NULL, 0, NULL);
	}
}

void SV_QCXReplayPhysics(void)
{
	if (ready) {
		Record(QCX_REPLAY_PHYSICS, QCX_REPLAY_NO_SLOT, NULL, 0, NULL);
	}
}

static void Finish(void)
{
	const qcx_replay_tape_t *tape = QCX_ReplayCapturedTape();
	uint64_t begin = UINT64_MAX, end = tape->count - 1;
	for (size_t i = 0; i < tape->count; ++i) {
		if (tape->events[i].kind == QCX_REPLAY_FRAME_BEGIN
			&& tape->events[i].clock.sv_time >= last_begin + warmup) {
			begin = i;
			break;
		}
	}
	if (!clients || begin == UINT64_MAX || begin >= end) {
		SV_Error("QCX capture lacks completed bootstrap and warmup segment");
	}
	Record(QCX_REPLAY_END, QCX_REPLAY_NO_SLOT, NULL, 0, NULL);
	QCX_ReplayCaptureIdentity(clients, asset_identity);
	if (!QCX_ReplayCaptureFinish(begin, end)) {
		SV_Error("QCX capture finish: %s", QCX_ReplayCaptureError());
	}
	Con_Printf("{\"qcx_probe_finished\":{\"clients\":%u,\"begin\":%llu,\"end\":%llu}}\n",
		clients, (unsigned long long)begin, (unsigned long long)end);
	ready = 0;
	finishing = 0;
}

void SV_QCXReplayFrameEnd(void)
{
	outer_active = 0;
	if (ready && QCX_ReplayMode() == QCX_REPLAY_RECORD) {
		Record(QCX_REPLAY_FRAME_END, QCX_REPLAY_NO_SLOT, NULL, 0, NULL);
		frame_open = 0;
		if (finishing) {
			Finish();
		}
	}
}

void SV_QCXReplayBootstrapEvent(qcx_replay_kind_t kind, client_t *client)
{
	if (!ready || QCX_ReplayMode() != QCX_REPLAY_RECORD) {
		return;
	}
	if (client->spectator || client->vip || client->rip_vip || client->isBot
		|| client->cuff_time > curtime || client->lockedtill > curtime) {
		SV_QCXReplayUnsupported("spectator/VIP/server bot bootstrap");
	}
	uint32_t slot = (uint32_t)(client - svs.clients);
	char info[MAX_EXT_INFO_STRING], payload[QCX_REPLAY_MAX_PAYLOAD];
	if (!Info_ReverseConvert(&client->_userinfo_ctx_, info, sizeof(info))) {
		SV_Error("QCX capture userinfo too large");
	}
	int n = snprintf(payload, sizeof(payload), "%d %u %a %a %a %a\n",
		client->userid, (unsigned)client->extensions, client->localtime,
		client->connection_started_realtime, client->connection_started_curtime,
		(double)client->last_check);
	/* SETUP/BEGIN record the actual SetNewParms output for later comparison;
	 * replay must execute SetNewParms, not install a saved replacement. */
	for (int i = 0; i < NUM_SPAWN_PARMS && n > 0 && (size_t)n < sizeof(payload); ++i) {
		int added = snprintf(payload + n, sizeof(payload) - (size_t)n, "%a ",
			(double)client->spawn_parms[i]);
		if (added < 0) {
			SV_Error("QCX capture could not format spawn parms");
		}
		n += added;
	}
	if (n > 0 && (size_t)n < sizeof(payload)) {
		n += snprintf(payload + n, sizeof(payload) - (size_t)n, "\n%s", info);
	}
	if (n < 0 || (size_t)n >= sizeof(payload)) {
		SV_Error("QCX capture bootstrap payload too large");
	}
	Record(kind, slot, payload, (size_t)n + 1, NULL);
	if (kind == QCX_REPLAY_ACCEPT && slot + 1 > clients) {
		clients = slot + 1;
	}
	if (kind == QCX_REPLAY_BEGIN) {
		last_begin = sv.time;
	}
}

void SV_QCXReplayGroupBegin(client_t *client)
{
	if (!ready || QCX_ReplayMode() != QCX_REPLAY_RECORD) {
		return;
	}
#ifdef MVD_PEXT1_SERVERSIDEWEAPON
	if (client->weaponswitch_enabled) {
		SV_QCXReplayUnsupported("server-side weapon selection");
	}
#endif
	group_open = 1;
	Record(QCX_REPLAY_GROUP_BEGIN, (uint32_t)(client - svs.clients), NULL, 0, NULL);
}

void SV_QCXReplayCommand(const usercmd_t *command)
{
	if (group_open) {
		Record(QCX_REPLAY_COMMAND, (uint32_t)(sv_client - svs.clients), NULL, 0, command);
	}
}

void SV_QCXReplayGroupEnd(client_t *client)
{
	if (group_open) {
		/* The packet's newcmd is passed by value into ExecuteClientMove.
		 * Its lastcmd assignment is not necessarily the mutated RunCmd copy. */
		Record(QCX_REPLAY_GROUP_END, (uint32_t)(client - svs.clients), NULL, 0, &client->lastcmd);
		group_open = 0;
	}
}

void SV_QCXReplayUserinfo(const char *key, const char *value)
{
	if (!ready || QCX_ReplayMode() != QCX_REPLAY_RECORD) {
		return;
	}
	if (sv_client->state == cs_spawned && (!strcmp(key, "name") || !strcmp(key, "team"))) {
		SV_QCXReplayUnsupported("post-bootstrap name/team change");
	}
	char payload[QCX_REPLAY_MAX_PAYLOAD];
	size_t a = strlen(key) + 1, b = strlen(value) + 1;
	if (a + b > sizeof(payload)) {
		SV_QCXReplayUnsupported("oversized setinfo");
	}
	memcpy(payload, key, a);
	memcpy(payload + a, value, b);
	Record(QCX_REPLAY_USERINFO, (uint32_t)(sv_client - svs.clients), payload, a+b, NULL);
}

static void Finish_f(void)
{
	if (!ready || QCX_ReplayMode() != QCX_REPLAY_RECORD) {
		Con_Printf("QCX capture is not active\n");
		return;
	}
	warmup = Cmd_Argc() == 2 ? atof(Cmd_Argv(1)) : 0;
	if (!isfinite(warmup) || warmup < 0) {
		Con_Printf("qcx_probe_finish [nonnegative warmup seconds]\n");
		return;
	}
	finishing = 1;
}

static void Status_f(void)
{
	char slots[256] = "";
	int comma = 0;
	for (unsigned i = 0; i < clients; ++i) {
		if (svs.clients[i].state == cs_spawned) {
			size_t used = strlen(slots);
			snprintf(slots + used, sizeof(slots) - used, "%s%u", comma ? "," : "", i);
			comma = 1;
		}
	}
	Con_Printf("{\"qcx_probe_status\":{\"spawncount\":%u,\"clients\":%u,\"events\":%zu,\"spawned\":[%s]}}\n",
		(unsigned)svs.spawncount, clients, QCX_ReplayCaptureCount(), slots);
}

void SV_QCXReplayInit(void)
{
	Cmd_AddCommand("qcx_probe_finish", Finish_f);
	Cmd_AddCommand("qcx_probe_status", Status_f);
}
