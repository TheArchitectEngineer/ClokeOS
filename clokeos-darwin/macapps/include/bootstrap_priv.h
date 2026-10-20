/* Ersatz für <bootstrap_priv.h> */
#ifndef CLOKE_BOOTSTRAP_PRIV_H
#define CLOKE_BOOTSTRAP_PRIV_H
#include <servers/bootstrap.h>
#define BOOTSTRAP_PER_PID_SERVICE     0x1
#define BOOTSTRAP_ALLOW_LOOKUP        0x2
#define BOOTSTRAP_DENY_JOB_CREATION   0x4
#define BOOTSTRAP_PRIVILEGED_SERVER   0x8
#define BOOTSTRAP_FORCE_LOCAL         0x10
#define BOOTSTRAP_SPECIFIC_INSTANCE   0x20
#define BOOTSTRAP_STRICT_CHECKIN      0x40
#define BOOTSTRAP_STRICT_LOOKUP       0x80
__BEGIN_DECLS
kern_return_t bootstrap_register2(mach_port_t bp, name_t service_name, mach_port_t sp, uint64_t flags);
kern_return_t bootstrap_look_up2(mach_port_t bp, const name_t service_name, mach_port_t *sp, pid_t target_pid, uint64_t flags);
__END_DECLS
#endif
