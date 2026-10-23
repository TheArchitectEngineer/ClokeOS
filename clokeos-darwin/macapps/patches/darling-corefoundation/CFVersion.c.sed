# Seit dem SDK 27 (Command Line Tools vom 21.10.2026) deklariert CFBase.h die Konstante als
# "const double"; Darlings Definition ohne const ist damit ein Übersetzungsfehler. Nur const
# ergänzen, Wert und Export bleiben (nichts in Darling schreibt darauf).
s/^double kCFCoreFoundationVersionNumber =/const double kCFCoreFoundationVersionNumber =/
