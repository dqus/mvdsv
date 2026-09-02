#ifndef MVDSV_QC2CPP_TEST_OBSERVER_H
#define MVDSV_QC2CPP_TEST_OBSERVER_H

#include <stdint.h>

void QCX_TestObserverRegisterCommands(void);
void QCX_TestObserverInitBegin(void);
void QCX_TestObserverInitEnd(void);
void QCX_TestObserverStartFrame(void);
void QCX_TestObserverNormalUnpublish(void);
void QCX_TestObserverTerminalUnpublish(void);
void QCX_TestObserverLegacyGameEntry(void);
void QCX_TestObserverClientConnect(uint32_t self);
void QCX_TestObserverPutClientInServer(uint32_t self, uint32_t spectator);
void QCX_TestObserverClientDisconnect(void);
void QCX_TestObserverClientKill(uint32_t self);
void QCX_TestObserverClientPreThink(void);
void QCX_TestObserverClientPostThink(uint32_t self, uint32_t spectator);
void QCX_TestObserverEdictThink(struct edict_s *thinking);
void QCX_TestObserverEdictTouch(struct edict_s *touched, struct edict_s *toucher);
void QCX_TestObserverRestoreReplicationBegin(void);
void QCX_TestObserverRestoreReplicationComplete(void);

#endif
