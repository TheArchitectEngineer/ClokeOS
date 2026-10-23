# arm64: -invoke geht über cfarm64.m (__CFArm64Invoke) statt über __invoke__ aus NSInvoke-x86.S.
/^- \(void\) _invokeUsingIMP: \(IMP\) imp withFrame: \(void \*\) frame$/,/^}$/ {
s|^    char rettype = \[_signature methodReturnType\]\[0\];$|#if defined(__arm64__)\
    extern void __CFArm64Invoke(NSMethodSignature *, IMP, void *, void *);\
    __CFArm64Invoke(_signature, imp, _retdata, frame);\
#else\
&|
s|^    if \(_retainedArgs\)$|#endif\
&|
}
