#ifndef MVDSV_QC2CPP_TEST_OBSERVER_H
#define MVDSV_QC2CPP_TEST_OBSERVER_H

#include <stdint.h>

void QC_TestObserverRegisterCommands(void);
void QC_TestObserverInitBegin(void);
void QC_TestObserverInitEnd(void);
void QC_TestObserverStartFrame(void);
void QC_TestObserverNormalUnpublish(void);
void QC_TestObserverLegacyGameEntry(void);

#endif
