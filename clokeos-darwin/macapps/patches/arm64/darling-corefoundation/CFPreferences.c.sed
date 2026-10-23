# gethostuuid mit {0, 0} wartet bei XNU unbegrenzt, bis die Plattform eine UUID veröffentlicht.
# PureDarwin tut das auf arm64 (QEMU virt) nie: jedes Programm, das Einstellungen liest
# (NSUserDefaults, also jedes AppKit-Programm), hing für immer im Kernel. Mit 1 ns Zeitlimit kommt
# die UUID auf x86_64 wie bisher sofort, auf arm64 kommt EWOULDBLOCK, und CF nimmt seinen eigenen
# Ersatz "UnknownHostID" für ByHost-Einstellungen, ohne bei jedem Start zu warnen.
s/^        struct timespec timeout = \{0, 0\};   \/\/ Infinite timeout for gethostuuid\(\)$/        struct timespec timeout = {0, 1};   \/\/ ClokeOS: nicht warten (arm64 hat keine Plattform-UUID)/
s/^            CFLog\(kCFLogLevelWarning, CFSTR\("_CFGetHostUUIDString: unable to determine UUID for host.*$/            \/\/ ClokeOS: ohne Warnung, auf arm64 gibt es nie eine Plattform-UUID/
