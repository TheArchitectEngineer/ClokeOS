// Vierter Test: async/await, Task, TaskGroup, actor (libswift_Concurrency, Executor über libdispatch).
@_silgen_name("exit") func c_exit(_ code: Int32) -> Never
actor Counter { var n = 0; func add(_ k: Int) -> Int { n += k; return n } }
func square(_ x: Int) async -> Int { x * x }
@main struct Main {
  static func main() async {
    let c = Counter()
    let total = await withTaskGroup(of: Int.self) { g in
      for i in 1...10 { g.addTask { let s = await square(i); _ = await c.add(s); return s } }
      var t = 0; for await v in g { t += v }; return t
    }
    let n = await c.add(0)
    print("sum", total, n)
    c_exit(total == 385 && n == 385 ? 0 : 1)
  }
}
