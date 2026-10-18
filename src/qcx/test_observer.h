#ifndef MVDSV_QC2CPP_TEST_OBSERVER_H
#define MVDSV_QC2CPP_TEST_OBSERVER_H

#include <stdint.h>

void QCX_TestObserverRegisterCommands(void);
void QCX_TestObserverInitBegin(void);
void QCX_TestObserverInitEnd(void);
void QCX_TestObserverGameplayImport(void);
void QCX_TestObserverServerStateBound(void);
void QCX_TestObserverStartFrame(void);
void QCX_TestObserverNormalUnpublish(void);
void QCX_TestObserverTerminalUnpublish(void);
void QCX_TestObserverLegacyGameEntry(void);
void QCX_TestObserverClientConnect(uint32_t self);
void QCX_TestObserverPutClientInServer(uint32_t self, uint32_t spectator);
void QCX_TestObserverClientDisconnect(void);
void QCX_TestObserverClientUserInfoChanged(uint32_t after);
void QCX_TestObserverClientCommand(void);
void QCX_TestObserverClientKill(uint32_t self);
void QCX_TestObserverClientPreThink(void);
void QCX_TestObserverClientPostThink(uint32_t self, uint32_t spectator);
void QCX_TestObserverEdictThink(struct edict_s *thinking);
void QCX_TestObserverEdictTouch(struct edict_s *touched, struct edict_s *toucher);
void QCX_TestObserverRestoreReplicationBegin(void);
void QCX_TestObserverRestoreReplicationComplete(void);
void QCX_TestObserverNqPreThink(struct edict_s *player, const float velocity[3], float teleport_time);
void QCX_TestObserverNqPostThink(struct edict_s *player, const float velocity[3]);
void QCX_TestObserverNqWaterJump(struct edict_s *player, float waterjumptime);
void QCX_TestObserverNewMissiles(uint32_t netquake);
void QCX_TestObserverNqProjectile(void);
void QCX_TestObserverNqMuzzleFlash(void);
void QCX_TestObserverNqIntermission(void);
void QCX_TestObserverNqInput(uint32_t buttons);

#endif
