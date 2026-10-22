/*
 * clokeos-netup: gibt allen Netzwerkkarten einen BSD-Namen (en0, en1, ...).
 *
 * Ein Treiber wie IOVirtIONet legt nur ein IOEthernetInterface an. Zu en0 wird es erst, wenn
 * jemand IONetworkStack darum bittet; auf macOS und im x86_64-Image von PureDarwin macht das
 * configd. Dem arm64-Image fehlt configd, deshalb erledigt das hier dieses Programm mit dem
 * Befehl "alle Schnittstellen registrieren" (IONetworkStackUserCommand = 2, siehe
 * IONetworkStack::setProperties in PureDarwins IONetworkingFamily). Braucht Root-Rechte.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdio.h>

int main(void)
{
	io_service_t stack = IOServiceGetMatchingService(MACH_PORT_NULL, IOServiceMatching("IONetworkStack"));
	if (!stack) {
		fprintf(stderr, "clokeos-netup: IONetworkStack nicht gefunden\n");
		return 1;
	}

	io_connect_t conn = 0;
	kern_return_t kr = IOServiceOpen(stack, mach_task_self(), 0, &conn);
	IOObjectRelease(stack);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "clokeos-netup: IOServiceOpen: 0x%x\n", kr);
		return 1;
	}

	int all = 2; /* kIONetworkStackRegisterInterfaceAll */
	CFNumberRef cmd = CFNumberCreate(NULL, kCFNumberIntType, &all);
	CFStringRef key = CFSTR("IONetworkStackUserCommand");
	CFDictionaryRef dict = CFDictionaryCreate(NULL, (const void **)&key, (const void **)&cmd, 1,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	kr = IOConnectSetCFProperties(conn, dict);
	CFRelease(dict);
	CFRelease(cmd);
	IOServiceClose(conn);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "clokeos-netup: Registrieren fehlgeschlagen: 0x%x\n", kr);
		return 1;
	}
	return 0;
}
