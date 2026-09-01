#ifndef MVDSV_QC2CPP_TEST_OBSERVER_H
#define MVDSV_QC2CPP_TEST_OBSERVER_H

#include <stdint.h>

void QC_TestObserverRegisterCommands(void);
void QC_TestObserverInitBegin(void);
void QC_TestObserverInitEnd(void);
void QC_TestObserverStartFrame(void);
void QC_TestObserverNormalUnpublish(void);
void QC_TestObserverLegacyGameEntry(void);
void QC_TestObserverClientConnect(void);
void QC_TestObserverPutClientInServer(uint32_t self, uint32_t spectator);
void QC_TestObserverClientDisconnect(void);
void QC_TestObserverClientKill(uint32_t self);
void QC_TestObserverClientPreThink(void);
void QC_TestObserverClientPostThink(uint32_t self, uint32_t spectator);
void QC_TestObserverEdictTouch(struct edict_s *touched, struct edict_s *toucher);

#endif
