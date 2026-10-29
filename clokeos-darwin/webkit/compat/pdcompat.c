/* Kleine Lücken zwischen Apples SDK-Headern und PureDarwins libSystem. */
#include <os/lock.h>
#include <stdlib.h>
#include <string.h>

/* PD hat nur os_unfair_lock_lock_with_options; die Flags (Datensynchronisation, Spin) haben dieselben Werte. */
extern void os_unfair_lock_lock_with_options(os_unfair_lock_t, uint32_t);
void os_unfair_lock_lock_with_flags(os_unfair_lock_t lock, uint32_t flags)
{
    os_unfair_lock_lock_with_options(lock, flags);
}

/* jsc ruft readline für die interaktive Eingabe; ohne libreadline im Image eine einfache Zeile von stdin. */
#include <stdio.h>
char* readline(const char* prompt)
{
    char buf[4096];
    if (prompt) fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(buf, sizeof buf, stdin)) return NULL;
    buf[strcspn(buf, "\n")] = 0;
    return strdup(buf);
}
int add_history(const char* line) { (void)line; return 0; }
