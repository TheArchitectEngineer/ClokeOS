# arm64: objc_msgSend, objc_msgSendSuper, method_invoke und IMPs nur mit Typumwandlung. Zwei
# Stellen wandelten das Ergebnis statt der Funktion um.
s/return method_invoke\(self, originalClassMethod\);/return ((Class (*)(id, Method))method_invoke)(self, originalClassMethod);/
s/observationInfo = cachedIMP\(object, @selector\(observationInfo\)\);/observationInfo = ((id (*)(id, SEL))cachedIMP)(object, @selector(observationInfo));/
s/\(void\)\(void \(\*\)\(id, SEL\)\)objc_msgSendSuper\(&super, _cmd\);/((void (*)(struct objc_super *, SEL))objc_msgSendSuper)(\&super, _cmd);/
s/return \(NSSet \*\)\(\(NSSet \* \(\*\)\(id, SEL\)\)objc_msgSend\(self, action\)\);/return ((NSSet * (*)(id, SEL))objc_msgSend)(self, action);/
s/observationInfo = kvcon\.cachedObservationInfoImplementation\(originalObservable, @selector\(observationInfo\)\);/observationInfo = ((id (*)(id, SEL))kvcon.cachedObservationInfoImplementation)(originalObservable, @selector(observationInfo));/
