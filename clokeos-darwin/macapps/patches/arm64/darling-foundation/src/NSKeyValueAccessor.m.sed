# arm64 kennt kein method_invoke_stret: Strukturen kommen dort über x8 (oder in Registern) zurück,
# method_invoke reicht. x86_64 bleibt wie bei Darling.
s/^(#define DEFINE_GET_VALUE_WITH_METHOD\(INFIX, TYPE\) \\)$/#if defined(__arm64__)\
#define method_invoke_stret method_invoke\
#endif\
\1/
