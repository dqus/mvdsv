#include "qwsvdef.h"
#include "qcx/replay_engine.h"
#include "qcx/adapter.h"
#include "qcx/entities.h"
#include "qcx/replay_checkpoint.h"

static int started, ready, frame_open, group_open, finishing, outer_active;
static unsigned clients;
static double last_begin, warmup;
static char asset_identity[65];
static unsigned checkpoint_frames;
static void CheckpointRecord(void);
extern void SV_PreRunCmd(void);
extern void SV_PostRunCmd(void);
extern void SV_RunCmd(usercmd_t *command, qbool inside, qbool second_attempt);

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
		"sv_mintic", "sv_maxtic", "sv_maxfps", "sv_maxspeed", "sv_gravity", "sv_accelerate",
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
	if (QCX_ReplayMode() == QCX_REPLAY_OFF) {
		return;
	}
	if (!QCX_Active()) {
		SV_Error("QCX capture did not load a QCX game");
	}
	snprintf(asset_identity, sizeof(asset_identity), "%08x-%04x",
		(unsigned)sv.map_checksum, (unsigned)CRC_Block((byte *)entities, (int)strlen(entities)));
	/* QWSP declares neither field. These extensions add source-visible
	 * mutations during packet output, outside this diagnostic workload. */
	if (fofs_visibility) {
		SV_Error("QCX replay does not support packet visibility fields");
	}
#ifdef MVD_PEXT1_HIGHLAGTELEPORT
	if (fofs_teleported) {
		SV_Error("QCX replay does not support packet teleport fields");
	}
#endif
	if (QCX_ReplayMode() != QCX_REPLAY_RECORD) {
		return;
	}
	QCX_ReplayCaptureIdentity(32, asset_identity);
	ready = 1;
	CheckpointRecord();
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
		QCX_REPLAY_COUNT(QCX_WORK_FRAME);
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
	while (end && tape->events[end].kind != QCX_REPLAY_FRAME_END) {
		--end;
	}
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
		if (++checkpoint_frames % 10 == 0 || finishing) {
			CheckpointRecord();
		}
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
	unsigned fte = 0, fte2 = 0, mvd = 0;
#ifdef PROTOCOL_VERSION_FTE
	fte = client->fteprotocolextensions;
#endif
#ifdef PROTOCOL_VERSION_FTE2
	fte2 = client->fteprotocolextensions2;
#endif
#ifdef PROTOCOL_VERSION_MVD1
	mvd = client->mvdprotocolextensions1;
#endif
	int n = snprintf(payload, sizeof(payload), "%d %u %u %u %u %a %a %a %a\n",
		client->userid, (unsigned)client->extensions, fte, fte2, mvd, client->localtime,
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

void SV_QCXReplayOutputEvent(client_t *client, unsigned fields)
{
	if (ready && QCX_ReplayMode() == QCX_REPLAY_RECORD) {
		uint8_t value = (uint8_t)fields;
		Record(QCX_REPLAY_OUTPUT, (uint32_t)(client - svs.clients), &value, 1, NULL);
	}
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

/* QCGD names its fields but does not carry wire type tags. This diagnostic
 * inventory comes from the retained generated schemas, not from C layouts. */
typedef struct { unsigned scope, kind; char name[128]; } field_kind_t;
static field_kind_t field_kinds[8192];
static size_t field_count;
static int FieldKind(unsigned scope, const char *name)
{
	for (size_t i = 0; i < field_count; ++i) {
		if (field_kinds[i].scope == scope && !strcmp(field_kinds[i].name, name)) {
			return (int)field_kinds[i].kind;
		}
	}
	return 0;
}
static void LoadFields(void)
{
	if (field_count) {
		return;
	}
	int option = COM_CheckParm("-qcx-probe-fields");
	FILE *file = option && option + 1 < com_argc ? fopen(com_argv[option+1], "r") : NULL;
	if (!file) {
		SV_Error("QCX replay requires -qcx-probe-fields inventory");
	}
	field_kind_t entry;
	int scanned;
	while ((scanned = fscanf(file, "%u %u %127s", &entry.scope, &entry.kind, entry.name)) == 3) {
		if (field_count == 8192 || entry.scope < 1 || entry.scope > 2
			|| entry.kind < 1 || entry.kind > 5 || FieldKind(entry.scope, entry.name)) {
			SV_Error("QCX replay invalid logical inventory");
		}
		field_kinds[field_count++] = entry;
	}
	if (scanned != EOF || !field_count || ferror(file)) {
		SV_Error("QCX replay malformed logical inventory");
	}
	fclose(file);
}

#define CHECKPOINT_WORDS (3 + QCX_WORK_COUNT)
#define CHECKPOINT_SIZE (8 * CHECKPOINT_WORDS)
static void Store64(uint8_t *out, uint64_t value)
{
	for (unsigned i = 0; i < 8; ++i) {
		out[i] = (uint8_t)(value >> (8*i));
	}
}
static uint64_t Read64(const uint8_t *in)
{
	uint64_t value = 0;
	for (unsigned i = 0; i < 8; ++i) {
		value |= (uint64_t)in[i] << (8*i);
	}
	return value;
}

static unsigned checkpoint_cells, checkpoint_number;
static uint64_t EngineCell(uint64_t hash, const char *name, const void *data, size_t size)
{
	uint64_t result = QCX_ReplayHashBytes(hash, data, size);
	if (COM_CheckParm("-qcx-probe-detail")) {
		Con_Printf("QCX cell %u %u %s %llx\n", checkpoint_number, checkpoint_cells, name,
			(unsigned long long)QCX_ReplayHashBytes(UINT64_C(14695981039346656037), data, size));
	}
	++checkpoint_cells;
	return result;
}

static void Checkpoint(uint8_t out[CHECKPOINT_SIZE])
{
	LoadFields();
	uint8_t bitmap[(MAX_EDICTS + 7) / 8] = {1};
	for (int i = 0; i < sv.num_edicts; ++i) {
		if (!EDICT_NUM(i)->e.free) {
			bitmap[i/8] |= 1U << (i%8);
		}
	}
	if (QCX_SetSaveSelection(bitmap, (QCX_EntityCapacity() + 7)/8) != QCX_RESTORE_OK) {
		SV_Error("QCX replay logical selection failed");
	}
	size_t size = QCX_SaveGuest(NULL, 0);
	uint8_t *data = malloc(size);
	uint64_t guest;
	if (!data || QCX_SaveGuest(data, size) != size
		|| !QCX_ReplayLogicalHash(data, size, FieldKind, &guest)) {
		SV_Error("QCX replay logical checkpoint: %s", QCX_ReplayCheckpointError());
	}
	free(data);
	checkpoint_cells = 0;
	++checkpoint_number;
	uint64_t engine = UINT64_C(14695981039346656037);
/* Explicit scalar cells only. No struct padding, pointers, callback payloads,
 * string handles, socket/packet statistics or network signon substates. */
#define CELL(value) engine = EngineCell(engine, #value, &(value), sizeof(value))
	CELL(sv.num_edicts); CELL(sv.max_edicts); CELL(sv.old_time); CELL(sv.old_bot_time); CELL(sv.physicstime);
	CELL(sv.lastcheck); CELL(sv.lastchecktime);
	globalvars_t *g = pr_global_struct;
	CELL(g->self); CELL(g->other); CELL(g->world); CELL(g->time); CELL(g->frametime);
	CELL(g->v_forward); CELL(g->v_up); CELL(g->v_right);
	CELL(g->trace_endpos); CELL(g->trace_plane_normal); CELL(g->trace_fraction);
	CELL(g->trace_plane_dist); CELL(g->trace_ent); CELL(g->trace_allsolid);
	CELL(g->trace_startsolid); CELL(g->trace_inopen); CELL(g->trace_inwater);
	for (int i = 0; i < sv.num_edicts; ++i) {
		edict_t *edict = EDICT_NUM(i);
		CELL(edict->e.free); CELL(edict->e.freetime); CELL(edict->e.lastruntime);
	}
	for (unsigned i = 0; i < clients; ++i) {
		client_t *client = &svs.clients[i];
		int spawned = client->state == cs_spawned;
		CELL(spawned); CELL(client->userid); CELL(client->spectator);
		CELL(client->entgravity); CELL(client->maxspeed);
		CELL(client->safestrafe.pending_frames); CELL(client->safestrafe.pending_direction);
		CELL(client->safestrafe.stop_frames); CELL(client->safestrafe.last_sidemove);
		CELL(client->lastcmd.msec); CELL(client->lastcmd.buttons); CELL(client->lastcmd.impulse);
		CELL(client->lastcmd.angles); CELL(client->lastcmd.forwardmove);
		CELL(client->lastcmd.sidemove); CELL(client->lastcmd.upmove);
		CELL(client->spawn_parms);
		const char *keys[] = {"name", "team", "railcolor", NULL};
		for (int k = 0; keys[k]; ++k) {
			const char *value = Info_Get(&client->_userinfo_ctx_, keys[k]);
			engine = EngineCell(engine, keys[k], value, strlen(value) + 1);
		}
	}
#undef CELL
	Store64(out, guest);
	Store64(out+8, engine);
	Store64(out+16, clients);
	for (unsigned i = 0; i < QCX_WORK_COUNT; ++i) {
		Store64(out+24+8*i, qcx_replay_work[i]);
	}
}
static void CheckpointRecord(void)
{
	uint8_t data[CHECKPOINT_SIZE];
	Checkpoint(data);
	Record(QCX_REPLAY_CHECKPOINT, QCX_REPLAY_NO_SLOT, data, sizeof(data), NULL);
}

static usercmd_t Command(const qcx_replay_command_t *source)
{
	usercmd_t command = {0};
	command.msec = source->msec;
	command.buttons = source->buttons;
	command.impulse = source->impulse;
	VectorCopy(source->angles, command.angles);
	command.forwardmove = source->forwardmove;
	command.sidemove = source->sidemove;
	command.upmove = source->upmove;
	return command;
}

static int Bootstrap(qcx_replay_event_t *event, int preflight)
{
	client_t *client = &svs.clients[event->slot];
	int userid, offset = 0;
	unsigned extensions, fte, fte2, mvd;
	double localtime, connected_rt, connected_ct, last_check;
	if (!event->payload_size || event->payload[event->payload_size-1] != 0
		|| memchr(event->payload, 0, event->payload_size-1)
		|| sscanf((const char *)event->payload, "%d %u %u %u %u %lf %lf %lf %lf\n%n",
			&userid, &extensions, &fte, &fte2, &mvd, &localtime, &connected_rt,
			&connected_ct, &last_check, &offset) != 9 || offset <= 0 || userid <= 0
		|| !isfinite(localtime) || !isfinite(connected_rt) || !isfinite(connected_ct)
		|| !isfinite(last_check)) {
		return 0;
	}
	const char *cursor = (const char *)event->payload + offset;
	float parms[NUM_SPAWN_PARMS];
	for (int i = 0; i < NUM_SPAWN_PARMS; ++i) {
		char *end;
		double value = strtod(cursor, &end);
		if (end == cursor || !isfinite(value)) {
			return 0;
		}
		parms[i] = (float)value;
		if (!isfinite(parms[i])) {
			return 0;
		}
		cursor = end;
	}
	if (cursor[0] != ' ' || cursor[1] != '\n') {
		return 0;
	}
	cursor += 2;
	if (strlen(cursor) >= MAX_EXT_INFO_STRING) {
		return 0;
	}
	if (preflight) {
		return 1;
	}
	if (event->kind == QCX_REPLAY_ACCEPT) {
		if (event->slot + 1 > clients) {
			clients = event->slot + 1;
		}
		memset(client, 0, sizeof(*client));
		client->userid = userid;
		client->state = cs_preconnected;
		client->_userinfo_ctx_.max = MAX_CLIENT_INFOS;
		client->_userinfoshort_ctx_.max = MAX_CLIENT_INFOS;
		Info_Convert(&client->_userinfo_ctx_, (char *)cursor);
		Netchan_Setup(NS_SERVER, &client->netchan, (netadr_t){0}, 0,
			Q_atoi(Info_Get(&client->_userinfo_ctx_, "mtu")));
		client->datagram = (sizebuf_t){.allowoverflow=true, .data=client->datagram_buf,
			.maxsize=sizeof(client->datagram_buf)};
		client->edict = EDICT_NUM(event->slot + 1);
		client->edict->e.free = false;
		PR_SetEntityString(client->edict, client->edict->v->netname, client->name);
		SV_ExtractFromUserinfo(client, true);
		client->disable_updates_stop = -1;
		SV_SetNewClientParms(client);
	}
	else {
		ctxinfo_t expected = {.max = MAX_CLIENT_INFOS};
		int equal = Info_Convert(&expected, (char *)cursor)
			&& expected.cur == client->_userinfo_ctx_.cur;
		for (info_t *item = expected.info_list; item && equal; item = item->next) {
			equal = !strcmp(item->value, Info_Get(&client->_userinfo_ctx_, item->name));
		}
		Info_RemoveAll(&expected);
		if (!equal) {
			Con_Printf("QCX replay bootstrap userinfo diverged\n");
			return 0;
		}
		for (int i = 0; i < NUM_SPAWN_PARMS; ++i) {
			if (parms[i] != client->spawn_parms[i]) {
				Con_Printf("QCX replay spawn parameter %d diverged\n", i);
				return 0;
			}
		}
	}
	/* Connection bookkeeping is explicit input, not a snapshot of QC world.
	 * These values change during normal signon before gameplay begins. */
	client->extensions = extensions;
#ifdef PROTOCOL_VERSION_FTE
	client->fteprotocolextensions = fte;
#endif
#ifdef PROTOCOL_VERSION_FTE2
	client->fteprotocolextensions2 = fte2;
#endif
#ifdef PROTOCOL_VERSION_MVD1
	client->mvdprotocolextensions1 = mvd;
#endif
	client->localtime = localtime;
	client->connection_started_realtime = connected_rt;
	client->connection_started_curtime = connected_ct;
	client->last_check = (float)last_check;
	sv_client = client;
	sv_player = client->edict;
	if (event->kind == QCX_REPLAY_SETUP) {
		client->state = cs_connected;
		SV_QCXReplaySetupClient(client);
	}
	else if (event->kind == QCX_REPLAY_BEGIN) {
		client->state = cs_spawned;
		SV_BeginClientGameplay();
	}
	return 1;
}

int SV_QCXReplayDispatch(qcx_replay_event_t *event, const qcx_replay_tape_t *tape)
{
	if (event->slot != QCX_REPLAY_NO_SLOT) {
		sv_client = &svs.clients[event->slot];
		sv_player = sv_client->edict;
	}
	switch (event->kind) {
	case QCX_REPLAY_MAP: {
		char configuration[sizeof(tape->config)], *save = NULL;
		strlcpy(configuration, tape->config, sizeof(configuration));
		for (char *entry = strtok_r(configuration, ";", &save); entry; entry = strtok_r(NULL, ";", &save)) {
			char *value = strchr(entry, '=');
			if (!value) {
				return 0;
			}
			*value++ = 0;
			cvar_t *var = Cvar_Find(entry);
			if (var) {
				Cvar_Set(var, value);
			}
			else if (strtod(value, NULL) != 0) {
				return 0;
			}
		}
		SV_SpawnServer((char *)tape->map, false, NULL, false, false);
		return QCX_Active() && !strcmp(asset_identity, tape->asset_hash);
	}
	case QCX_REPLAY_ACCEPT: case QCX_REPLAY_SETUP: case QCX_REPLAY_BEGIN:
		return Bootstrap(event, 0);
	case QCX_REPLAY_FRAME_BEGIN:
		QCX_REPLAY_COUNT(QCX_WORK_FRAME);
		QCX_ReplayRand();
		break;
	case QCX_REPLAY_GROUP_BEGIN:
		sv_client->localtime = sv.time;
		SV_PreRunCmd();
		break;
	case QCX_REPLAY_COMMAND: {
		usercmd_t command = Command(&event->command);
		SV_RunCmd(&command, false, false);
		break;
	}
	case QCX_REPLAY_GROUP_END:
		SV_PostRunCmd();
		sv_client->lastcmd = Command(&event->command);
		break;
	case QCX_REPLAY_PHYSICS:
		SV_Physics();
#ifdef USE_PR2
		/* Even without server bots this updates canonical frametime. QCX's
		 * bot-frame callback itself is intentionally skipped by the engine. */
		SV_RunBots();
#endif
		break;
	case QCX_REPLAY_FRAME_END:
		for (unsigned i = 0; i < tape->clients; ++i) {
			SV_ClearReliable(&svs.clients[i]);
			SZ_Clear(&svs.clients[i].datagram);
		}
		SZ_Clear(&sv.datagram);
		SZ_Clear(&sv.reliable_datagram);
		break;
	case QCX_REPLAY_OUTPUT:
		SV_QCXReplayApplyOutput(sv_client, event->payload[0]);
		break;
	case QCX_REPLAY_USERINFO: {
		const char *key = (const char *)event->payload;
		size_t size = event->payload_size;
		const char *end = size ? memchr(key, 0, size) : NULL;
		if (!end || end + 1 == key + size || key[size-1] != 0) {
			return 0;
		}
		SV_QCXReplayApplyUserinfo(key, end + 1);
		break;
	}
	case QCX_REPLAY_CHECKPOINT: {
		uint8_t actual[CHECKPOINT_SIZE];
		Checkpoint(actual);
		for (unsigned i = 0; i < CHECKPOINT_WORDS; ++i) {
			if (Read64(actual + 8*i) != Read64(event->payload + 8*i)) {
				Con_Printf("QCX replay checkpoint %llu word %u diverged: expected %llx actual %llx\n",
					(unsigned long long)event->sequence, i,
					(unsigned long long)Read64(event->payload + 8*i),
					(unsigned long long)Read64(actual + 8*i));
				return 0;
			}
		}
		break;
	}
	case QCX_REPLAY_END:
		break;
	default:
		return 0;
	}
	return 1;
}

int SV_QCXReplayRun(void)
{
	qcx_replay_tape_t tape = {0};
	if (sv.state == ss_active || !QCX_ReplayTapeLoad(QCX_ReplayPath(), &tape)) {
		Con_Printf("QCX replay requires an unspawned fresh server and valid tape\n");
		return 0;
	}
	unsigned checkpoints = 0;
	for (size_t i = 0; i < tape.count; ++i) {
		qcx_replay_event_t *event = &tape.events[i];
		if (event->kind >= QCX_REPLAY_ACCEPT && event->kind <= QCX_REPLAY_BEGIN
			&& !Bootstrap(event, 1)) {
			Con_Printf("QCX replay invalid bootstrap payload before execution\n");
			QCX_ReplayTapeFree(&tape);
			return 0;
		}
		if (event->kind == QCX_REPLAY_CHECKPOINT) {
			++checkpoints;
		}
	}
	if (checkpoints < 2 || tape.events[tape.count-2].kind != QCX_REPLAY_CHECKPOINT) {
		Con_Printf("QCX replay requires initial and final checkpoints\n");
		QCX_ReplayTapeFree(&tape);
		return 0;
	}
	int result = QCX_ReplayExecute(&tape, QCX_ReplayMode());
	char work[512] = "";
	for (unsigned i = 0; i < QCX_WORK_COUNT; ++i) {
		size_t used = strlen(work);
		snprintf(work + used, sizeof(work)-used, "%s%llu", i ? "," : "",
			(unsigned long long)qcx_replay_work[i]);
	}
	Con_Printf("{\"qcx_probe_execution\":{\"ok\":%s,\"events\":%zu,\"checkpoints\":%u,\"begin\":%llu,\"end\":%llu,\"work\":[%s]}}\n",
		result ? "true" : "false", tape.count, checkpoints,
		(unsigned long long)tape.timed_begin, (unsigned long long)tape.timed_end, work);
	QCX_ReplayTapeFree(&tape);
	return result;
}
