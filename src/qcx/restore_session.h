#ifndef MVDSV_QC2CPP_RESTORE_SESSION_H
#define MVDSV_QC2CPP_RESTORE_SESSION_H

#include "qwsvdef.h"
#include "qcx/save_format.h"

#include <stdint.h>

typedef struct qcx_restore_session_status_s {
	qbool waiting;
	double remaining_seconds;
	uint32_t available_count;
	uint32_t bound_count;
	uint32_t active_count;
} qcx_restore_session_status_t;

qbool QCX_RestoreSessionInstall(const qcx_save_image_t *image,
	double monotonic_now);
void QCX_RestoreSessionInit(void);
void QCX_RestoreSessionContinue(void);
void QCX_RestoreSessionCancel(void);
qbool QCX_RestoreSessionBlocksSave(void);
qbool QCX_RestoreSessionWaiting(void);
void QCX_RestoreSessionGetStatus(qcx_restore_session_status_t *out,
	double monotonic_now);
qbool QCX_RestoreSessionSlotReserved(uint32_t slot);
qbool QCX_RestoreSessionAdmissionRole(const char *raw_name, qbool *spectator);
client_t *QCX_RestoreSessionAdmissionSlot(const char *raw_name);
void QCX_RestoreSessionRememberAdmissionIdentity(client_t *client,
	qbool requested_spectator);
void QCX_RestoreSessionObserveClient(client_t *client);
void QCX_RestoreSessionNameChanged(client_t *client);
void QCX_RestoreSessionFrame(double monotonic_now);
qbool QCX_RestoreSessionClientWaiting(const client_t *client);
qbool QCX_RestoreSessionClientPending(const client_t *client);
qbool QCX_RestoreSessionClientRestoresGameplay(const client_t *client);
qbool QCX_RestoreSessionClientRoleLocked(const client_t *client);
void QCX_RestoreSessionPrintRoster(client_t *client);
qbool QCX_RestoreSessionPrepareSpawn(client_t *client);
qbool QCX_RestoreSessionBegin(client_t *client);
qbool QCX_RestoreSessionClientDropped(client_t *client);

#endif
