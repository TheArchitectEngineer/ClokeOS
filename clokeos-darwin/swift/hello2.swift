// Zweiter Test: Klassen/ARC, Generics, Enums mit Payload, Fehler, Mirror, KeyPaths, Hashing, Sortieren, String-Interpolation.
@_silgen_name("exit") func c_exit(_ code: Int32) -> Never
enum Op: Error { case div0, neg(Int) }
indirect enum Tree { case leaf(Int); case node(Tree, Tree) }
func sum(_ t: Tree) -> Int { switch t { case .leaf(let v): return v; case .node(let l, let r): return sum(l) + sum(r) } }
func safeDiv(_ a: Int, _ b: Int) throws -> Int { if b == 0 { throw Op.div0 }; if b < 0 { throw Op.neg(b) }; return a / b }
final class Box<T: Comparable> { var v: [T]; init(_ v: [T]) { self.v = v }; deinit { print("deinit Box") } }
struct P { var x = 1, y = "s"; let z: [Int?] = [1, nil] }
var fails = 0
func check(_ c: Bool, _ m: String) { if !c { print("FAIL", m); fails += 1 } }
do {
  let b = Box([5, 3, 9, 1])
  b.v.sort(); check(b.v == [1, 3, 5, 9], "sort")
  check(sum(.node(.leaf(2), .node(.leaf(3), .leaf(4)))) == 9, "tree")
  do { _ = try safeDiv(1, 0) } catch Op.div0 { print("caught div0") }
  do { _ = try safeDiv(1, -2) } catch Op.neg(let n) { check(n == -2, "neg") }
  print(String(describing: P()))
  print(Mirror(reflecting: P()).children.map { "\($0.label!)" }.joined(separator: " "))
  check(\P.x == \P.x, "keypath")
  var s = Set<String>(); for i in 0..<1000 { s.insert("k\(i)") }; check(s.count == 1000 && s.contains("k999"), "set")
  var rng = SystemRandomNumberGenerator(); check((0..<100).contains(Int.random(in: 0..<100, using: &rng)), "rng")
  print(3.14159, 1e100, Double.pi * 2, Float(1) / 3, "ünï😀".count, "ünï😀".utf8.count, "a,b,c".split(separator: ",").count)
  print(type(of: b), String(reflecting: Op.neg(2)))
  let big = (1...20).reduce(1 as UInt64, *); check(big == 2432902008176640000, "fact")
}
print(fails == 0 ? "ALL OK" : "FAILED \(fails)")
c_exit(Int32(fails))
