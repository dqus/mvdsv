#ifndef MVDSV_QC2CPP_SERVICES_H
#define MVDSV_QC2CPP_SERVICES_H

#include "game/plugin_api.h"

/* Mandatory V1 host table service groups. */
void QCX_BindWorldServices(qcx_host_api_v1_t *host);
void QCX_BindMovementServices(qcx_host_api_v1_t *host);
void QCX_BindNetworkServices(qcx_host_api_v1_t *host);

#if defined(QCX_TESTS)
#include "qcx/test_observer.h"
#define QCX_ObserveGameplayImport(context) do { \
	(void)(context); \
	QCX_TestObserverGameplayImport(); \
} while (0)
#else
#define QCX_ObserveGameplayImport(context) do { (void)(context); } while (0)
#endif

#endif
