#include "qcx/save.h"

#include "qcx/adapter.h"
#include "qcx/entities.h"
#include "qcx/restore_session.h"
#include "qcx/world_text.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static qcx_save_image_t *qcx_prepared_image;

typedef struct qcx_restore_plan_s {
	const qcx_save_image_t *image;
	uint32_t num_edicts;
	uint8_t selection[(QCX_SAVE_MAX_ENTITY_CAPACITY + 7U) / 8U];
} qcx_restore_plan_t;

static qcx_restore_plan_t qcx_restore_plan;
_Static_assert(QCX_SAVE_MAX_RESOURCE_BYTES == QCX_MAX_LIGHTSTYLE_BYTES,
	"lightstyle storage must hold validated save resources");
_Static_assert(QCX_SAVE_LIGHTSTYLE_COUNT == MAX_LIGHTSTYLES,
	"QCMS lightstyle count must match the engine");
_Static_assert(QCX_SAVE_MODEL_CAPACITY == MAX_MODELS && QCX_SAVE_SOUND_CAPACITY == MAX_SOUNDS,
	"QCMS precache bounds must match the engine");
_Static_assert(QCX_SAVE_MAX_ENTITY_CAPACITY == MAX_EDICTS,
	"QCMS entity capacity must match the engine");
_Static_assert(NUM_SPAWN_PARMS == QCX_SAVE_SPAWN_PARM_COUNT,
	"QCMS spawn parameter count");

static uint32_t QCX_SaveBoundedStringLength(const char *value, uint32_t limit)
{
	uint32_t length;
	if (value == NULL) {
		return 0U;
	}
	for (length = 0U; length <= limit; ++length) {
		if (value[length] == '\0') {
			return length;
		}
	}
	return limit + 1U;
}

static qbool QCX_SaveCapturePrecache(qcx_save_engine_t *engine, char kind,
	uint32_t slot, const char *name)
{
	qcx_save_precache_t *entry = &engine->precaches[engine->precache_count++];
	entry->kind = kind;
	entry->slot = slot;
	return strlcpy(entry->name, name, sizeof(entry->name)) < sizeof(entry->name);
}

static qbool QCX_SaveCaptureEngine(uint32_t entity_capacity, qcx_save_engine_t *engine)
{
	uint32_t precache_count = 0U;
	int index;
	if (sv.max_edicts <= 0 || entity_capacity == 0U
		|| entity_capacity > QCX_SAVE_MAX_ENTITY_CAPACITY
		|| (uint32_t)sv.max_edicts > entity_capacity || sv.num_edicts <= 0
		|| sv.num_edicts > sv.max_edicts) {
		return false;
	}
	engine->time = sv.time;
	engine->serverflags = svs.serverflags;
	for (index = 0; index < MAX_LIGHTSTYLES; ++index) {
		const char *value = sv.lightstyles[index] == NULL ? "m" : sv.lightstyles[index];
		if (strlcpy(engine->lightstyles[index], value, sizeof(engine->lightstyles[index]))
			>= sizeof(engine->lightstyles[index])) {
			return false;
		}
	}
	for (index = 1; index < MAX_MODELS; ++index) {
		if (sv.model_precache[index] != NULL && sv.model_precache[index][0] != '\0') {
			++precache_count;
		}
	}
	for (index = 1; index < MAX_SOUNDS; ++index) {
		if (sv.sound_precache[index] != NULL && sv.sound_precache[index][0] != '\0') {
			++precache_count;
		}
	}
	if (precache_count != 0U) {
		engine->precaches = calloc(precache_count, sizeof(*engine->precaches));
		if (engine->precaches == NULL) {
			return false;
		}
	}
	for (index = 1; index < MAX_MODELS; ++index) {
		if (sv.model_precache[index] != NULL && sv.model_precache[index][0] != '\0'
			&& !QCX_SaveCapturePrecache(engine, 'M', (uint32_t)index, sv.model_precache[index])) {
			return false;
		}
	}
	for (index = 1; index < MAX_SOUNDS; ++index) {
		if (sv.sound_precache[index] != NULL && sv.sound_precache[index][0] != '\0'
			&& !QCX_SaveCapturePrecache(engine, 'S', (uint32_t)index, sv.sound_precache[index])) {
			return false;
		}
	}
	for (index = 0; index < (int)entity_capacity; ++index) {
		engine->edicts[index].state = index >= sv.num_edicts ? QCX_SAVE_UNUSED_EDICT
			: sv.edicts[index].e.free ? QCX_SAVE_FREE_EDICT : QCX_SAVE_ACTIVE_EDICT;
		engine->edicts[index].freetime = sv.edicts[index].e.freetime;
	}
	return true;
}

static qbool QCX_SaveBuildRoster(qcx_save_image_t *image)
{
	uint32_t slot;
	image->roster_count = 0U;
	for (slot = 0U; slot < MAX_CLIENTS; ++slot) {
		const client_t *const client = &svs.clients[slot];
		qcx_save_roster_entry_t *entry;
		uint32_t name_size;
		uint32_t team_size;
		if (client->state != cs_connected && client->state != cs_spawned) continue;
		if (image->roster_count == QCX_SAVE_MAX_CLIENTS) return false;
		name_size = QCX_SaveBoundedStringLength(client->name,
			QCX_SAVE_CLIENT_STRING_CAPACITY - 1U);
		team_size = QCX_SaveBoundedStringLength(client->team,
			QCX_SAVE_CLIENT_STRING_CAPACITY - 1U);
		if (name_size == 0U || name_size >= QCX_SAVE_CLIENT_STRING_CAPACITY
			|| team_size >= QCX_SAVE_CLIENT_STRING_CAPACITY) {
			return false;
		}
		entry = &image->roster[image->roster_count++];
		entry->saved_slot = slot;
		entry->spawned = client->state == cs_spawned;
		entry->role = client->spectator != 0 ? QCX_SAVE_ROLE_SPECTATOR
			: QCX_SAVE_ROLE_PLAYER;
		memcpy(entry->name, client->name, name_size + 1U);
		memcpy(entry->team, client->team, team_size + 1U);
		memcpy(entry->spawn_parms, client->spawn_parms, sizeof(entry->spawn_parms));
	}
	return true;
}

static void QCX_SaveSelection(uint8_t *bitmap, uint32_t size)
{
	int slot;
	memset(bitmap, 0, size);
	for (slot = 0; slot < sv.num_edicts; ++slot) if (!sv.edicts[slot].e.free) bitmap[(uint32_t)slot / 8U] |= (uint8_t)(1U << ((uint32_t)slot % 8U));
}

static qbool QCX_SaveNameIsSafe(const char *name)
{
	uint32_t index;
	const uint32_t size = QCX_SaveBoundedStringLength(name, QCX_SAVE_NAME_CAPACITY - 1U);
	if (size == 0U || size >= QCX_SAVE_NAME_CAPACITY) return false;
	for (index = 0U; index < size; ++index) {
		const unsigned char byte = (unsigned char)name[index];
		if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_' || byte == '-')) return false;
	}
	return true;
}

static qbool QCX_SaveWriteFile(const char *name, const uint8_t *bytes, uint32_t size)
{
	char path[MAX_OSPATH], temporary[MAX_OSPATH]; FILE *file;
	qbool written;
	qbool flushed;
	int error = 0;
	const int path_size = snprintf(path, sizeof(path), "%s/save/%s.sav", fs_gamedir, name);
	if (path_size < 0 || (size_t)path_size >= sizeof(path) || snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0 || strlen(temporary) >= sizeof(temporary)) return false;
	FS_CreatePath(path);
	file = fopen(temporary, "wb");
	if (file == NULL) {
		Con_Printf("qc2cpp save write failed for %s: %s.\n", temporary, strerror(errno));
		return false;
	}
	written = fwrite(bytes, 1U, size, file) == size;
	if (!written) error = errno;
	flushed = written && fflush(file) == 0;
	if (!flushed && error == 0) error = errno;
	if (fclose(file) != 0 && error == 0) error = errno;
	if (!written || !flushed || error != 0) {
		Con_Printf("qc2cpp save write failed for %s: %s.\n", temporary, strerror(error));
		remove(temporary);
		return false;
	}
	if (rename(temporary, path) != 0) {
		Con_Printf("qc2cpp save commit failed for %s: %s.\n", path, strerror(errno));
		remove(temporary);
		return false;
	}
	FS_FlushFSHash(); return true;
}

static qcx_restore_status_t QCX_SaveCheckEngineIdentity(const qcx_save_image_t *image)
{
	uint8_t models[MAX_MODELS] = {0};
	uint8_t sounds[MAX_SOUNDS] = {0};
	uint32_t index;
	if (!QCX_SaveImageValid(image)) {
		return QCX_RESTORE_MALFORMED_CHUNK;
	}
	if (image->metadata.entity_capacity != QCX_EntityCapacity()
		|| strcmp(image->metadata.logical_game, sv_progsname.string) != 0
		|| strcmp(image->metadata.map_name, sv.mapname) != 0
		|| image->metadata.map_bsp_checksum != sv.map_checksum) {
		Con_Printf("qc2cpp restore engine identity mismatch: game=%s map=%s capacity=%u checksum=%u.\n",
			sv_progsname.string, sv.mapname, QCX_EntityCapacity(), sv.map_checksum);
		return QCX_RESTORE_ENTITY_SET_MISMATCH;
	}
	/* Structural validation belongs to save_format. Here compare the parsed
	 * resources with the world that will receive the saved gameplay state. */
	for (index = 0U; index < image->engine.precache_count; ++index) {
		const qcx_save_precache_t *entry = &image->engine.precaches[index];
		const char *actual = entry->kind == 'M' ? sv.model_precache[entry->slot]
			: sv.sound_precache[entry->slot];
		if (actual == NULL || strcmp(entry->name, actual) != 0) {
			Con_Printf("qc2cpp restore %s mismatch at %u: expected %s, got %s.\n",
				entry->kind == 'M' ? "model" : "sound", entry->slot,
				entry->name, actual == NULL ? "<none>" : actual);
			return QCX_RESTORE_ENTITY_SET_MISMATCH;
		}
		if (entry->kind == 'M') {
			models[entry->slot] = 1U;
		} else {
			sounds[entry->slot] = 1U;
		}
	}
	if (qcx_prepared_image == NULL) {
		for (index = 1U; index < MAX_MODELS; ++index) {
			if ((sv.model_precache[index] != NULL && sv.model_precache[index][0] != '\0')
				!= (models[index] != 0U)) {
				return QCX_RESTORE_ENTITY_SET_MISMATCH;
			}
		}
		for (index = 1U; index < MAX_SOUNDS; ++index) {
			if ((sv.sound_precache[index] != NULL && sv.sound_precache[index][0] != '\0')
				!= (sounds[index] != 0U)) {
				return QCX_RESTORE_ENTITY_SET_MISMATCH;
			}
		}
	}
	memset(qcx_restore_plan.selection, 0, sizeof(qcx_restore_plan.selection));
	qcx_restore_plan.num_edicts = 0U;
	for (index = 0U; index < image->metadata.entity_capacity; ++index) {
		const qcx_save_edict_state_t state = image->engine.edicts[index].state;
		if (state != QCX_SAVE_UNUSED_EDICT) {
			qcx_restore_plan.num_edicts = index + 1U;
		}
		if (state == QCX_SAVE_ACTIVE_EDICT) {
			qcx_restore_plan.selection[index / 8U] |= (uint8_t)(1U << (index % 8U));
		}
	}
	return QCX_RESTORE_OK;
}

qcx_restore_status_t QCX_ValidateSaveGame(const qcx_save_image_t *image)
{
	qcx_restore_status_t status;
	qcx_restore_plan.image = NULL;
	status = QCX_SaveCheckEngineIdentity(image);
	if (status != QCX_RESTORE_OK) return status;
	status = QCX_ValidateGuestRestore(image->guest_payload.data, image->guest_payload.size,
		qcx_restore_plan.selection, (image->metadata.entity_capacity + 7U) / 8U);
	if (status != QCX_RESTORE_OK) return status;
	qcx_restore_plan.image = image;
	return QCX_RESTORE_OK;
}

void QCX_RefreshClientReplication(void)
{
	uint32_t source_index;
	for (source_index = 0U; source_index < MAX_CLIENTS; ++source_index) {
		client_t *const source = &svs.clients[source_index];
		uint32_t recipient_index;
		if (source->state != cs_connected && source->state != cs_spawned) continue;
		source->delta_sequence = -1;
		for (recipient_index = 0U; recipient_index < MAX_CLIENTS; ++recipient_index) {
			client_t *const recipient = &svs.clients[recipient_index];
			if (recipient->state < cs_preconnected) continue;
			SV_FullClientUpdateToClient(source, recipient);
			/* A load can happen between client packets; make the rebuilt reliable
			 * stream eligible for the next server send rather than waiting for a
			 * later movement command. */
			recipient->send_message = true;
		}
	}
}

void QCX_ApplySaveGame(const qcx_save_image_t *image)
{
	uint32_t index;
	if (image == NULL || qcx_restore_plan.image != image
		|| QCX_SetSaveSelection(qcx_restore_plan.selection,
			(image->metadata.entity_capacity + 7U) / 8U) != QCX_RESTORE_OK
		|| QCX_RestoreGuest(image->guest_payload.data, image->guest_payload.size) != QCX_RESTORE_OK) {
		SV_Error("qc2cpp restore failed after commit");
	}
	for (index = 0U; index < MAX_LIGHTSTYLES; ++index) {
		sv.lightstyles[index] = QCX_StoreLightstyle(index, image->engine.lightstyles[index]);
	}
	for (index = 0U; index < image->metadata.entity_capacity; ++index) {
		sv.edicts[index].e.free = image->engine.edicts[index].state != QCX_SAVE_ACTIVE_EDICT;
		sv.edicts[index].e.freetime = image->engine.edicts[index].freetime;
	}
	/* Player edicts are reserved engine slots, not ordinary game objects.  A
	 * roster-free player slot therefore must not become visible merely because
	 * the guest's fixed entity storage contains a value at that index. */
	for (index = 0U; index < image->metadata.client_slot_capacity; ++index) {
		qbool restored_client = false;
		uint32_t client_index;
		for (client_index = 0U; client_index < image->roster_count; ++client_index) {
			if (image->roster[client_index].saved_slot == index) {
				restored_client = true;
				break;
			}
		}
		if (!restored_client) sv.edicts[index + 1U].e.free = true;
	}
	sv.num_edicts = (int)qcx_restore_plan.num_edicts;
	sv.time = image->engine.time;
	sv.old_time = sv.time;
	svs.serverflags = image->engine.serverflags;
	PR_GLOBAL(serverflags) = svs.serverflags;
	SV_ClearWorld();
	for (index = 0U; index < qcx_restore_plan.num_edicts; ++index) {
		if (!sv.edicts[index].e.free) SV_LinkEdict(&sv.edicts[index], false);
	}
	qcx_restore_plan.image = NULL;
}

qbool QCX_SaveGame(const char *name)
{
	uint8_t selection[(QCX_SAVE_MAX_ENTITY_CAPACITY + 7U) / 8U];
	const uint32_t entity_capacity = QCX_EntityCapacity();
	const uint32_t selection_size = (entity_capacity + 7U) / 8U;
	qcx_save_bytes_t guest = {0}; qcx_save_image_t image = {0};
	uint8_t *encoded = NULL; uint32_t encoded_size = 0U; qbool result = false;
	const char *failure = "unknown error";
	qcx_byte_count_t required;
	if (QCX_RestoreSessionBlocksSave() || QCX_HasPreparedLoadGame()) {
		Con_Printf("qc2cpp save rejected: restore is still in progress.\n");
		return false;
	}
	if (!QCX_Active() || sv.state != ss_active || sv.max_edicts <= 0 || entity_capacity == 0U
		|| entity_capacity > QCX_SAVE_MAX_ENTITY_CAPACITY || (uint32_t)sv.max_edicts > entity_capacity
		|| !QCX_SaveNameIsSafe(name)) {
		Con_Printf("qc2cpp save rejected: inactive or incompatible server state.\n");
		return false;
	}
	QCX_SaveSelection(selection, selection_size);
	if (QCX_SetSaveSelection(selection, selection_size) != QCX_RESTORE_OK) {
		Con_Printf("qc2cpp save rejected: game rejected entity selection.\n");
		return false;
	}
	required = QCX_SaveGuest(NULL, 0U);
	if (required == 0U || required > QCX_SAVE_MAX_GUEST_BYTES) {
		Con_Printf("qc2cpp save rejected: invalid game snapshot size.\n");
		return false;
	}
	guest.size = required;
	if (guest.size != 0U) {
		guest.data = malloc(guest.size);
		if (guest.data == NULL || QCX_SaveGuest(guest.data, guest.size) != guest.size) {
			failure = "could not serialize game state";
			goto done;
		}
	}
	if (!QCX_SaveCaptureEngine(entity_capacity, &image.engine)) {
		failure = "could not serialize server state";
		goto done;
	}
	if (!QCX_SaveBuildRoster(&image)) {
		failure = "could not serialize restored player roster";
		goto done;
	}
	strlcpy(image.metadata.logical_game, sv_progsname.string, sizeof(image.metadata.logical_game));
	strlcpy(image.metadata.map_name, sv.mapname, sizeof(image.metadata.map_name));
	image.metadata.map_bsp_checksum = sv.map_checksum;
	image.metadata.entity_capacity = entity_capacity;
	image.metadata.client_slot_capacity = MAX_CLIENTS;
	image.guest_payload = guest;
	if (QCX_SaveEncode(&image, &encoded, &encoded_size) != QCX_PLUGIN_OK) {
		failure = "could not encode QCMS image";
		goto done;
	}
	result = QCX_SaveWriteFile(name, encoded, encoded_size);
	if (!result) failure = "could not write save file";
done:
	if (!result) Con_Printf("qc2cpp save rejected: %s.\n", failure);
	free(encoded);
	free(image.engine.precaches);
	free(guest.data);
	return result;
}

static qbool QCX_SaveReadFile(const char *name, uint8_t **out, uint32_t *out_size)
{
	char path[MAX_OSPATH];
	FILE *file;
	long length;
	uint8_t *bytes;
	qbool read;
	int close_status;
	const int path_size = snprintf(path, sizeof(path), "%s/save/%s.sav", fs_gamedir, name);
	*out = NULL;
	*out_size = 0U;
	if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
	file = fopen(path, "rb");
	if (file == NULL || fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) <= 0L
		|| (uint64_t)length > QCX_SAVE_MAX_FILE_BYTES || fseek(file, 0L, SEEK_SET) != 0) {
		if (file != NULL) fclose(file);
		return false;
	}
	bytes = malloc((size_t)length);
	if (bytes == NULL) {
		fclose(file);
		return false;
	}
	read = fread(bytes, 1U, (size_t)length, file) == (size_t)length;
	close_status = fclose(file);
	if (!read || close_status != 0) {
		free(bytes);
		return false;
	}
	*out = bytes;
	*out_size = (uint32_t)length;
	return true;
}

void QCX_DiscardPreparedLoadGame(void)
{
	QCX_SaveImageFree(qcx_prepared_image);
	qcx_prepared_image = NULL;
}

qbool QCX_HasPreparedLoadGame(void)
{
	return qcx_prepared_image != NULL;
}

static qbool QCX_SaveIsV1Image(const uint8_t *bytes, uint32_t size)
{
	return bytes != NULL && size >= 8U && memcmp(bytes, "QCMS", 4U) == 0
		&& bytes[4] == 1U && bytes[5] == 0U && bytes[6] == 0U && bytes[7] == 0U;
}

qbool QCX_PrepareLoadGame(const char *name, char *map_name, uint32_t map_name_size)
{
	uint8_t *bytes = NULL;
	uint32_t size = 0U;
	qcx_save_image_t *image = NULL;
	qcx_plugin_status_t parse_status;
	if (map_name == NULL || map_name_size == 0U || !QCX_SaveNameIsSafe(name)
		|| !QCX_SaveReadFile(name, &bytes, &size)) {
		return false;
	}
	parse_status = QCX_SaveParse(bytes, size, &image);
	if (parse_status != QCX_PLUGIN_OK) {
		if (QCX_SaveIsV1Image(bytes, size)) {
			Con_Printf("qc2cpp restore rejected: QCMS V1 saves are unsupported; create a new save.\n");
		}
		QCX_SaveImageFree(image);
		free(bytes);
		return false;
	}
	if (strcmp(image->metadata.logical_game, sv_progsname.string) != 0
		|| QCX_SaveBoundedStringLength(image->metadata.map_name, map_name_size - 1U)
		>= map_name_size) {
		Con_Printf("qc2cpp fresh restore rejected: saved game=%s, selected game=%s, map=%s, roster=%u.\n",
			image->metadata.logical_game, sv_progsname.string, image->metadata.map_name,
			(unsigned)image->roster_count);
		QCX_SaveImageFree(image);
		free(bytes);
		return false;
	}
	QCX_DiscardPreparedLoadGame();
	qcx_prepared_image = image;
	strlcpy(map_name, image->metadata.map_name, map_name_size);
	free(bytes);
	return true;
}

qbool QCX_PrepareLoadResources(void)
{
	uint32_t index;
	if (qcx_prepared_image == NULL) {
		return false;
	}
	/* PR_LoadProgs may invoke a qc2cpp host call before SV_SpawnServer installs
	 * its normal slot-zero sentinels. Keep the indexed precache arrays
	 * traversable while restoring their already-validated saved order. */
	sv.model_precache[0] = "";
	sv.sound_precache[0] = "";
	for (index = 0U; index < qcx_prepared_image->engine.precache_count; ++index) {
		const qcx_save_precache_t *entry = &qcx_prepared_image->engine.precaches[index];
		const size_t size = strlen(entry->name) + 1U;
		char *persistent = Hunk_Alloc((int)size);
		memcpy(persistent, entry->name, size);
		if (entry->kind == 'M') {
			sv.model_precache[entry->slot] = persistent;
		} else {
			sv.sound_precache[entry->slot] = persistent;
		}
	}
	return true;
}

qbool QCX_CommitPreparedLoadGame(void)
{
	qcx_restore_status_t status;
	if (qcx_prepared_image == NULL) return false;
	status = QCX_ValidateSaveGame(qcx_prepared_image);
	if (status != QCX_RESTORE_OK) {
		Con_Printf("qc2cpp prepared restore rejected: status %u.\n", (unsigned)status);
		return false;
	}
	QCX_ApplySaveGame(qcx_prepared_image);
	if (!QCX_RestoreSessionInstall(qcx_prepared_image, Sys_DoubleTime())) {
		Con_Printf("qc2cpp prepared restore could not install player roster.\n");
		return false;
	}
	QCX_DiscardPreparedLoadGame();
	return true;
}
