/* Ersatz für <xpc/private.h> (nur was Darlings NSXPCConnection braucht) */
#ifndef CLOKE_XPC_PRIVATE_H
#define CLOKE_XPC_PRIVATE_H
#include <xpc/xpc.h>
#include <bsm/audit.h>
__BEGIN_DECLS
void xpc_connection_get_audit_token(xpc_connection_t connection, audit_token_t *token);
__END_DECLS
#endif
