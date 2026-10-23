# arm64: objc_msgSend nur mit Typumwandlung (siehe darling-corefoundation/CFRuntime.c.sed)
s/\((BOOL|NSComparisonResult)\)objc_msgSend\(/((\1 (*)(id, SEL, id))objc_msgSend)(/g
