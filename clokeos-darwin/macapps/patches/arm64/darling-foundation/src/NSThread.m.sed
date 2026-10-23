# arm64: objc_msgSend nur mit Typumwandlung (siehe darling-corefoundation/CFRuntime.c.sed)
s/^(        )objc_msgSend\((_target, _selector, _argument|self, aSelector, arg)\);$/\1((void (*)(id, SEL, id))objc_msgSend)(\2);/
