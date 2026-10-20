/* Ersatz für <dispatch/private.h> (libdispatch, nur was CoreFoundation braucht). */
#ifndef CLOKE_DISPATCH_PRIVATE_H
#define CLOKE_DISPATCH_PRIVATE_H
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <stdbool.h>

#define DISPATCH_QUEUE_OVERCOMMIT 0x2ull

__BEGIN_DECLS
typedef mach_port_t dispatch_runloop_handle_t;
void _dispatch_main_queue_callback_4CF(void *msg);
mach_port_t _dispatch_get_main_queue_port_4CF(void);
dispatch_queue_t _dispatch_runloop_root_queue_create_4CF(const char *label, unsigned long flags);
mach_port_t _dispatch_runloop_root_queue_get_port_4CF(dispatch_queue_t queue);
bool _dispatch_runloop_root_queue_perform_4CF(dispatch_queue_t queue);
__END_DECLS

/* fehlt in PureDarwin: libdispatch markiert damit Timer, die der Runloop selbst bedient.
 * dispatch_source_set_timer stellt denselben Timer, nur ohne diese Markierung. */
static inline void _dispatch_source_set_runloop_timer_4CF(dispatch_source_t source, dispatch_time_t start,
                                                          uint64_t interval, uint64_t leeway) {
    dispatch_source_set_timer(source, start, interval, leeway);
}
#endif
