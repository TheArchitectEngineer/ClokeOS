// Dritter Test: Swift-Klasse von NSObject (libobjc, ObjC-Metadaten aus dem Swift-Compiler), Aufruf über die ObjC-Runtime.
import ObjectiveC
@_silgen_name("exit") func c_exit(_ code: Int32) -> Never
final class Foo: NSObject { var n = 7; func hi() -> Int { 42 } }
let f = Foo()
print(f.hi(), f.n, f.isKind(of: NSObject.self), String(cString: class_getName(type(of: f))), f.hash == f.hash)
print(class_getSuperclass(type(of: f)) == NSObject.self)
c_exit(f.hi() == 42 ? 0 : 1)
