# Darling-Fehler: -initWithTitle:action:keyEquivalent: setzte keinen Modifikator, Apple setzt dort ⌘
# (NSEventModifierFlagCommand). Kürzel wie "q" galten so ohne ⌘, und Menüs zeigten "Q" statt "⌘Q".
# Aus Nibs gelesene Einträge bringen ihre eigene Maske mit und bleiben unberührt.
/^- initWithTitle: \(NSString \*\) title$/,/^}/ s/^    _keyEquivalentModifierMask = 0;$/    _keyEquivalentModifierMask = NSCommandKeyMask;/
