# arm64: Apples SDK erlaubt dort kein objc_msgSend ohne Typumwandlung (variadische Argumente
# lägen auf dem Stack, nicht in x0/x1). Mit Umwandlung ist der Aufruf auf beiden Architekturen
# derselbe.
s/= objc_msgSend\(objc_msgSend\((cls[A-Za-z]+), selAlloc\), selInit\);/= ((id (*)(id, SEL))objc_msgSend)(((id (*)(id, SEL))objc_msgSend)((id)\1, selAlloc), selInit);/
# CFEqual mit einem ObjC-Objekt: isEqual: bekam sein Argument variadisch, auf arm64 also auf dem
# Stack statt in x2.
s/\(\(rettype \(\*\)\(CFTypeRef, SEL, \.\.\.\)\)objc_msgSend\)\(obj, sel_getUid\(#sel\), a1\)/((rettype (*)(CFTypeRef, SEL, CFTypeRef))objc_msgSend)(obj, sel_getUid(#sel), a1)/
