/* Ersatz für <vproc_priv.h> */
#ifndef CLOKE_VPROC_PRIV_H
#define CLOKE_VPROC_PRIV_H
#include <stddef.h>
#include <vproc.h>
/* fehlt in PureDarwin: Transaktionen für sudden termination; ohne launchd-Unterstützung wirkungslos */
static inline void _vproc_transactions_enable(void) {}
static inline vproc_transaction_t _vproc_transaction_begin(void) { return (vproc_transaction_t)0; }
static inline void _vproc_transaction_end(void) {}
static inline size_t _vproc_transaction_count(void) { return 0; }
static inline void _vproc_transaction_try_exit(int status) { (void)status; }
#endif
