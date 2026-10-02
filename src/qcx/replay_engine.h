#ifndef MVDSV_QCX_REPLAY_ENGINE_H
#define MVDSV_QCX_REPLAY_ENGINE_H

#include "qcx/replay_probe.h"
struct client_s;
struct usercmd_s;

void SV_QCXReplayInit(void);
void SV_QCXReplayMapStart(const char *map, int restoring);
void SV_QCXReplayMapReady(const char *entities);
void SV_QCXReplayFrameBegin(void);
void SV_QCXReplayPhysics(void);
void SV_QCXReplayFrameEnd(void);
void SV_QCXReplayBootstrapEvent(qcx_replay_kind_t kind, struct client_s *client);
void SV_QCXReplayGroupBegin(struct client_s *client);
void SV_QCXReplayCommand(const struct usercmd_s *command);
void SV_QCXReplayGroupEnd(struct client_s *client);
void SV_QCXReplayUserinfo(const char *key, const char *value);
void SV_QCXReplayUnsupported(const char *reason);
void SV_QCXReplayClientCommand(const char *name);
void SV_BeginClientGameplay(void);
void SV_QCXReplaySetupClient(struct client_s *client);
int SV_QCXReplayRun(void);
void SV_QCXReplayOutputEvent(struct client_s *client, unsigned fields);
void SV_QCXReplayApplyOutput(struct client_s *client, unsigned fields);
void SV_QCXReplayApplyUserinfo(const char *key, const char *value);

#endif
