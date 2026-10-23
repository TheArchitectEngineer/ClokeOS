# arm64: IMPs nur mit Typumwandlung aufrufen (sheetDidEnd:returnCode:contextInfo:)
s/^([[:space:]]+)function\(\[context modalDelegate\], \[context didEndSelector\],/\1((void (*)(id, SEL, NSWindow *, NSInteger, void *))function)([context modalDelegate], [context didEndSelector],/
