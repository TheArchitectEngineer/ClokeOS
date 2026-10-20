/* Ersatz für <os/voucher_private.h> */
#ifndef CLOKE_OS_VOUCHER_PRIVATE_H
#define CLOKE_OS_VOUCHER_PRIVATE_H
#include <mach/mach.h>
#include <os/object.h>
typedef struct voucher_s *voucher_t;
typedef struct voucher_mach_msg_state_s *voucher_mach_msg_state_t;
#define VOUCHER_MACH_MSG_STATE_UNCHANGED ((voucher_mach_msg_state_t)~0ul)
__BEGIN_DECLS
voucher_mach_msg_state_t voucher_mach_msg_adopt(mach_msg_header_t *msg);
void voucher_mach_msg_revert(voucher_mach_msg_state_t state);
__END_DECLS
/* fehlt in PureDarwin */
static inline voucher_t voucher_copy(void) { return (voucher_t)0; }
#endif
