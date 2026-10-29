// Swift-Hello für ClokeOS: print, struct, protocol, closure, String, Array, Dictionary.
// Kein `import Darwin` (libswiftDarwin.dylib ist Apple-intern); exit() kommt direkt aus libSystem.
@_silgen_name("exit") func c_exit(_ code: Int32) -> Never

protocol Shape { var name: String { get }; func area() -> Double }
struct Rect: Shape { let w, h: Double; var name: String { "rect" }; func area() -> Double { w * h } }
struct Circle: Shape { let r: Double; var name: String { "circle" }; func area() -> Double { 3.0 * r * r } }

let shapes: [any Shape] = [Rect(w: 2, h: 3), Circle(r: 2)]
let total = shapes.map { $0.area() }.reduce(0, +)
for s in shapes { print("\(s.name): \(s.area())") }
print("total = \(total)")
var words = "ClokeOS runs Swift".split(separator: " ").map { String($0).uppercased() }
words.sort()
print(words.joined(separator: ","), words.count)
var counter = 0
let inc = { counter += 1 }
for _ in 0..<5 { inc() }
print("counter", counter)
let d: [String: Int] = ["a": 1, "b": 2]
print(d.values.reduce(0, +))
c_exit(counter == 5 ? 3 : 1)
