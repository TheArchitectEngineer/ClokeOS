# arm64: IMPs nur mit Typumwandlung aufrufen (-layoutNextFragment)
s/^([[:space:]]+)_layoutNextFragment\(self, NULL\);/\1((void (*)(id, SEL))_layoutNextFragment)(self, NULL);/
