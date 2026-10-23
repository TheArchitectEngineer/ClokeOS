/* NSInvocation-Probe: prüft, was auf arm64 an der Aufrufkonvention hängt (cfarm64.m): NSInvocation
 * mit vielen Argumentarten, Weiterleitung (forwardInvocation:, forwardingTargetForSelector:, NSProxy,
 * NSUndoManager), unbekannte Selektoren als Ausnahme, KVC mit Strukturen, KVO, CFEqual mit
 * Objective-C-Objekten, NSPredicate-Vergleiche, NSThread. Wie fprobe.m ein Programm aus Apples SDK;
 * auf dem Mac läuft es gegen Apples Foundation, unter ClokeOS gegen Darlings, die Ausgabe muss
 * gleich sein.
 *
 *   clang -arch x86_64 -arch arm64 -mmacosx-version-min=11.0 -fobjc-arc -framework Foundation \
 *     -o invprobe macapps/invprobe.m
 *   DYLD_FRAMEWORK_PATH=/Library/ClokeOS/MacFrameworks ./invprobe       (im Gast) */
#import <Foundation/Foundation.h>
#import <objc/message.h>
#include <stdio.h>

static int ok, bad;
static void check(const char *name, BOOL cond, NSString *detail) {
  if (cond) { ok++; printf("OK      %s\n", name); }
  else { bad++; printf("FEHLER  %s: %s\n", name, detail ? detail.UTF8String : "?"); }
  fflush(stdout);
}
#define TEST(tname, ...)                                                                    \
  do {                                                                                      \
    @try { __VA_ARGS__ } @catch (NSException *e) {                                          \
      check(tname, NO, [NSString stringWithFormat:@"Ausnahme %@: %@", e.name, e.reason]); } \
  } while (0)

typedef struct { double a, b, c, d, e; } Big;          /* 40 Byte: per Verweis, Rückgabe über x8 */
typedef struct { float x, y, z; } F3;                  /* float-HFA */
typedef struct { char c; short s; int i; } Small;      /* 8 Byte in einem x-Register */
typedef struct { long long q; char c; } Pair;          /* 16 Byte, zwei x-Register */

@interface Ziel : NSObject
@property NSRange range;
@property NSPoint point;
@property NSSize size;
@property NSRect rect;
@property(copy) NSString *name;
@property int zahl;
@end

@implementation Ziel
- (NSRect)rectMit:(double)a f:(float)f c:(char)c r:(NSRange)r big:(Big)big f3:(F3)f3 s:(short)s b:(BOOL)b {
  return NSMakeRect(a + f + c + r.location + big.a + big.e + f3.x + f3.z, r.length + s + (b ? 1000 : 0),
                    big.c * 2, f3.y);
}
- (Big)bigMit:(int)a :(int)b :(int)c :(int)d :(int)e :(int)f :(int)g :(int)h :(int)i :(char)j :(short)k
          :(double)d1 :(double)d2 :(double)d3 :(double)d4 :(double)d5 :(double)d6 :(double)d7 :(double)d8
          :(double)d9 :(float)f10 :(NSPoint)p {
  Big r = { a + b + c + d + e + f + g + h + i, j * 100 + k, d1 + d2 + d3 + d4 + d5 + d6 + d7 + d8,
            d9 * f10, p.x - p.y };
  return r;
}
- (F3)f3Mit:(F3)a und:(F3)b { F3 r = { a.x + b.x, a.y + b.y, a.z * b.z }; return r; }
- (Small)smallMit:(Small)s { Small r = { (char)(s.c + 1), (short)(s.s * 2), s.i - 7 }; return r; }
- (Pair)pairMit:(Pair)p und:(long long)q { Pair r = { p.q + q, (char)(p.c - 1) }; return r; }
- (char)zeichen:(char)c { return (char)(c - 3); }
- (unsigned short)kurz:(unsigned short)s { return (unsigned short)(s + 1); }
- (BOOL)ja:(BOOL)b { return !b; }
- (float)fl:(float)a :(double)b { return a * (float)b; }
- (double)viele:(double)a :(double)b :(double)c :(double)d :(double)e :(double)f :(double)g :(double)h
              :(double)i :(float)j :(double)k { return a + b + c + d + e + f + g + h + i * 10 + j * 100 + k * 1000; }
- (NSString *)text:(NSString *)s zahl:(long)n { return [NSString stringWithFormat:@"%@/%ld", s, n]; }
@end

/* leitet alles per forwardInvocation: an ein Ziel weiter */
@interface Weiterleiter : NSObject { @public Ziel *_ziel; int _zaehler; }
@end
@implementation Weiterleiter
- (NSMethodSignature *)methodSignatureForSelector:(SEL)sel {
  return [super methodSignatureForSelector:sel] ?: [_ziel methodSignatureForSelector:sel];
}
- (void)forwardInvocation:(NSInvocation *)inv {
  _zaehler++;
  if ([_ziel respondsToSelector:inv.selector]) [inv invokeWithTarget:_ziel];
  else [super forwardInvocation:inv];
}
@end

@interface Abkuerzung : NSObject { @public Ziel *_ziel; }
@end
@implementation Abkuerzung
- (id)forwardingTargetForSelector:(SEL)sel { return _ziel; }
@end

@interface Stellvertreter : NSProxy { @public Ziel *_ziel; }
@end
@implementation Stellvertreter
- (NSMethodSignature *)methodSignatureForSelector:(SEL)sel { return [_ziel methodSignatureForSelector:sel]; }
- (void)forwardInvocation:(NSInvocation *)inv { inv.target = _ziel; [inv invoke]; }
@end

@interface Gleich : NSObject { @public int _wert; }
@end
@implementation Gleich
- (BOOL)isEqual:(id)o { return [o isKindOfClass:[Gleich class]] && ((Gleich *)o)->_wert == _wert; }
- (NSUInteger)hash { return (NSUInteger)_wert; }
@end

@interface Beobachter : NSObject { @public NSMutableArray *_log; }
@end
@implementation Beobachter
- (void)observeValueForKeyPath:(NSString *)k ofObject:(id)o change:(NSDictionary *)c context:(void *)ctx {
  [_log addObject:[NSString stringWithFormat:@"%@=%@", k, c[NSKeyValueChangeNewKey]]];
}
@end

@interface Faden : NSObject { @public NSString *_got; NSCondition *_cond; }
- (void)lauf:(NSString *)s;
@end
@implementation Faden
- (void)lauf:(NSString *)s {
  [_cond lock]; _got = [s stringByAppendingString:@"!"]; [_cond signal]; [_cond unlock];
}
@end

static NSString *rectText(NSRect r) {
  return [NSString stringWithFormat:@"%g %g %g %g", r.origin.x, r.origin.y, r.size.width, r.size.height];
}

int main(int argc, const char **argv) {
  @autoreleasepool {
    printf("NSInvocation-Probe 1\n");
    Ziel *ziel = [Ziel new];
    Big big = { 1.5, 2, 3.25, 4, 5.5 };
    F3 f3a = { 1.5f, 2.5f, 3.f }, f3b = { 10.f, 20.f, -2.f };
    NSRange r = NSMakeRange(7, 9);

    TEST("NSMethodSignature", {
      NSMethodSignature *sig = [ziel methodSignatureForSelector:@selector(rectMit:f:c:r:big:f3:s:b:)];
      check("NSMethodSignature", sig.numberOfArguments == 10 && sig.methodReturnLength == sizeof(NSRect) &&
                                     strcmp([sig getArgumentTypeAtIndex:6], @encode(Big)) == 0,
            [NSString stringWithFormat:@"%lu %lu", (unsigned long)sig.numberOfArguments, (unsigned long)sig.methodReturnLength]);
    });

    TEST("invoke: NSRect, Big, float-HFA, NSRange, char, short, BOOL", {
      SEL sel = @selector(rectMit:f:c:r:big:f3:s:b:);
      NSInvocation *inv = [NSInvocation invocationWithMethodSignature:[ziel methodSignatureForSelector:sel]];
      double a = 0.25; float f = 0.5f; char c = -3; short s = -300; BOOL b = YES;
      inv.target = ziel; inv.selector = sel;
      [inv setArgument:&a atIndex:2]; [inv setArgument:&f atIndex:3]; [inv setArgument:&c atIndex:4];
      [inv setArgument:&r atIndex:5]; [inv setArgument:&big atIndex:6]; [inv setArgument:&f3a atIndex:7];
      [inv setArgument:&s atIndex:8]; [inv setArgument:&b atIndex:9];
      [inv invoke];
      NSRect got; [inv getReturnValue:&got];
      NSRect want = [ziel rectMit:a f:f c:c r:r big:big f3:f3a s:s b:b];
      Big back; [inv getArgument:&back atIndex:6];
      check("invoke: NSRect, Big, float-HFA, NSRange, char, short, BOOL",
            NSEqualRects(got, want) && memcmp(&back, &big, sizeof big) == 0, rectText(got));
    });

    TEST("invoke: 22 Argumente auf dem Stack, Rückgabe über x8", {
      SEL sel = @selector(bigMit::::::::::::::::::::::);
      NSInvocation *inv = [NSInvocation invocationWithMethodSignature:[ziel methodSignatureForSelector:sel]];
      inv.target = ziel; inv.selector = sel;
      for (int i = 0; i < 9; i++) { int v = i + 1; [inv setArgument:&v atIndex:2 + i]; }
      char j = 7; short k = -5; [inv setArgument:&j atIndex:11]; [inv setArgument:&k atIndex:12];
      for (int i = 0; i < 9; i++) { double v = i * 1.5; [inv setArgument:&v atIndex:13 + i]; }
      float f10 = 4.f; NSPoint p = NSMakePoint(9, 4);
      [inv setArgument:&f10 atIndex:22]; [inv setArgument:&p atIndex:23];
      [inv invoke];
      Big got; [inv getReturnValue:&got];
      Big want = [ziel bigMit:1 :2 :3 :4 :5 :6 :7 :8 :9 :7 :-5 :0 :1.5 :3 :4.5 :6 :7.5 :9 :10.5 :12 :4 :p];
      check("invoke: 22 Argumente auf dem Stack, Rückgabe über x8", memcmp(&got, &want, sizeof got) == 0,
            [NSString stringWithFormat:@"%g %g %g %g %g", got.a, got.b, got.c, got.d, got.e]);
    });

    TEST("invoke: kleine Typen und float", {
      NSInvocation *inv = [NSInvocation invocationWithMethodSignature:[ziel methodSignatureForSelector:@selector(zeichen:)]];
      char c = -100; inv.target = ziel; inv.selector = @selector(zeichen:); [inv setArgument:&c atIndex:2]; [inv invoke];
      char rc; [inv getReturnValue:&rc];
      inv = [NSInvocation invocationWithMethodSignature:[ziel methodSignatureForSelector:@selector(fl::)]];
      float a = 1.5f; double b = 4; inv.target = ziel; inv.selector = @selector(fl::);
      [inv setArgument:&a atIndex:2]; [inv setArgument:&b atIndex:3]; [inv invoke];
      float rf; [inv getReturnValue:&rf];
      inv = [NSInvocation invocationWithMethodSignature:[ziel methodSignatureForSelector:@selector(smallMit:)]];
      Small sm = { 5, -6, 70000 }; inv.target = ziel; inv.selector = @selector(smallMit:); [inv setArgument:&sm atIndex:2];
      [inv invoke]; Small rs; [inv getReturnValue:&rs];
      check("invoke: kleine Typen und float", rc == -103 && rf == 6.f && rs.c == 6 && rs.s == -12 && rs.i == 69993,
            [NSString stringWithFormat:@"%d %g %d %d %d", rc, rf, rs.c, rs.s, rs.i]);
    });

    TEST("forwardInvocation:", {
      Weiterleiter *w = [Weiterleiter new]; w->_ziel = ziel;
      id x = w;
      NSRect got = [x rectMit:0.25 f:0.5f c:-3 r:r big:big f3:f3a s:-300 b:NO];
      NSRect want = [ziel rectMit:0.25 f:0.5f c:-3 r:r big:big f3:f3a s:-300 b:NO];
      NSPoint p = NSMakePoint(9, 4);
      Big gb = [x bigMit:1 :2 :3 :4 :5 :6 :7 :8 :9 :7 :-5 :0 :1.5 :3 :4.5 :6 :7.5 :9 :10.5 :12 :4 :p];
      Big wb = [ziel bigMit:1 :2 :3 :4 :5 :6 :7 :8 :9 :7 :-5 :0 :1.5 :3 :4.5 :6 :7.5 :9 :10.5 :12 :4 :p];
      F3 gf = [x f3Mit:f3a und:f3b], wf = [ziel f3Mit:f3a und:f3b];
      Small sm = { 5, -6, 70000 }; Small gs = [x smallMit:sm];
      Pair pr = { 1LL << 40, 'z' }; Pair gp = [x pairMit:pr und:5];
      char gc = [x zeichen:-100]; unsigned short gk = [x kurz:65534]; BOOL gj = [x ja:NO];
      double gd = [x viele:1 :2 :3 :4 :5 :6 :7 :8 :9 :0.5f :2];
      NSString *gt = [x text:@"abc" zahl:-42];
      check("forwardInvocation:", NSEqualRects(got, want) && memcmp(&gb, &wb, sizeof gb) == 0 &&
                                    memcmp(&gf, &wf, sizeof gf) == 0 && gs.i == 69993 && gs.c == 6 &&
                                    gp.q == (1LL << 40) + 5 && gp.c == 'y' && gc == -103 && gk == 65535 && gj &&
                                    gd == [ziel viele:1 :2 :3 :4 :5 :6 :7 :8 :9 :0.5f :2] &&
                                    [gt isEqualToString:@"abc/-42"] && w->_zaehler == 10,
            [NSString stringWithFormat:@"%@ | %g %g | %g %g %g | %d %lld %d %d %u %d %g %@ %d", rectText(got), gb.a, gb.e,
                                       gf.x, gf.y, gf.z, gs.i, gp.q, gp.c, gc, gk, gj, gd, gt, w->_zaehler]);
    });

    TEST("forwardingTargetForSelector:", {
      Abkuerzung *a = [Abkuerzung new]; a->_ziel = ziel;
      id x = a;
      NSRect got = [x rectMit:1 f:2 c:3 r:r big:big f3:f3b s:4 b:YES];
      check("forwardingTargetForSelector:", NSEqualRects(got, [ziel rectMit:1 f:2 c:3 r:r big:big f3:f3b s:4 b:YES]) &&
                                              [[x text:@"z" zahl:1] isEqualToString:@"z/1"], rectText(got));
    });

    TEST("NSProxy", {
      Stellvertreter *p = [Stellvertreter alloc]; p->_ziel = ziel;
      id x = p;
      F3 gf = [x f3Mit:f3b und:f3a], wf = [ziel f3Mit:f3b und:f3a];
      check("NSProxy", memcmp(&gf, &wf, sizeof gf) == 0 && [x ja:YES] == NO && [x isKindOfClass:[Ziel class]],
            [NSString stringWithFormat:@"%g %g %g", gf.x, gf.y, gf.z]);
    });

    TEST("unbekannter Selektor", {
      NSString *name = nil;
      @try { ((void (*)(id, SEL, id))objc_msgSend)([NSObject new], NSSelectorFromString(@"gibtEsNicht:"), nil); }
      @catch (NSException *e) { name = e.name; }
      Weiterleiter *w = [Weiterleiter new]; w->_ziel = ziel;
      NSString *name2 = nil;
      @try { ((void (*)(id, SEL))objc_msgSend)(w, NSSelectorFromString(@"auchNicht")); }
      @catch (NSException *e) { name2 = e.name; }
      check("unbekannter Selektor", [name isEqualToString:NSInvalidArgumentException] &&
                                      [name2 isEqualToString:NSInvalidArgumentException],
            [NSString stringWithFormat:@"%@ %@", name, name2]);
    });

    TEST("NSUndoManager", {
      NSUndoManager *um = [NSUndoManager new];
      um.groupsByEvent = NO;
      ziel.rect = NSMakeRect(1, 2, 3, 4);
      [um beginUndoGrouping];
      [[um prepareWithInvocationTarget:ziel] setRect:ziel.rect];
      [[um prepareWithInvocationTarget:ziel] setName:@"vorher"];
      [um endUndoGrouping];
      ziel.rect = NSMakeRect(9, 9, 9, 9); ziel.name = @"nachher";
      [um undo];
      check("NSUndoManager", NSEqualRects(ziel.rect, NSMakeRect(1, 2, 3, 4)) && [ziel.name isEqualToString:@"vorher"],
            [NSString stringWithFormat:@"%@ %@", rectText(ziel.rect), ziel.name]);
    });

    TEST("KVC mit Strukturen", {
      ziel.range = NSMakeRange(3, 4); ziel.point = NSMakePoint(5, 6); ziel.size = NSMakeSize(7, 8);
      ziel.rect = NSMakeRect(1, 2, 3, 4); ziel.zahl = -17;
      NSRange gr = [[ziel valueForKey:@"range"] rangeValue];
      NSPoint gp = [[ziel valueForKey:@"point"] pointValue];
      NSSize gs = [[ziel valueForKey:@"size"] sizeValue];
      NSRect gR = [[ziel valueForKey:@"rect"] rectValue];
      int gz = [[ziel valueForKey:@"zahl"] intValue];
      [ziel setValue:[NSValue valueWithRect:NSMakeRect(4, 3, 2, 1)] forKey:@"rect"];
      check("KVC mit Strukturen", gr.location == 3 && gr.length == 4 && gp.x == 5 && gp.y == 6 && gs.width == 7 &&
                                    gs.height == 8 && NSEqualRects(gR, NSMakeRect(1, 2, 3, 4)) && gz == -17 &&
                                    NSEqualRects(ziel.rect, NSMakeRect(4, 3, 2, 1)),
            [NSString stringWithFormat:@"%lu %lu | %g %g | %g %g | %@ | %d", (unsigned long)gr.location,
                                       (unsigned long)gr.length, gp.x, gp.y, gs.width, gs.height, rectText(gR), gz]);
    });

    TEST("KVO", {
      Beobachter *b = [Beobachter new]; b->_log = [NSMutableArray array];
      Ziel *z = [Ziel new];
      [z addObserver:b forKeyPath:@"name" options:NSKeyValueObservingOptionNew context:NULL];
      [z addObserver:b forKeyPath:@"zahl" options:NSKeyValueObservingOptionNew context:NULL];
      z.name = @"eins"; z.zahl = 2;
      [z setValue:@"drei" forKey:@"name"];
      [z removeObserver:b forKeyPath:@"name"]; [z removeObserver:b forKeyPath:@"zahl"];
      z.name = @"vier";
      check("KVO", [[b->_log componentsJoinedByString:@","] isEqualToString:@"name=eins,zahl=2,name=drei"] &&
                     [z class] == [Ziel class],
            [b->_log componentsJoinedByString:@","]);
    });

    TEST("CFEqual mit Objective-C-Objekten", {
      Gleich *a = [Gleich new], *b = [Gleich new], *c = [Gleich new];
      a->_wert = 5; b->_wert = 5; c->_wert = 6;
      NSSet *set = [NSSet setWithObjects:a, b, c, nil];
      check("CFEqual mit Objective-C-Objekten", CFEqual((__bridge CFTypeRef)a, (__bridge CFTypeRef)b) &&
                                                  !CFEqual((__bridge CFTypeRef)a, (__bridge CFTypeRef)c) && set.count == 2 &&
                                                  [@[a] containsObject:b],
            [NSString stringWithFormat:@"%lu", (unsigned long)set.count]);
    });

    TEST("NSPredicate", {
      NSArray *zahlen = @[@1, @4, @7, @10];
      NSArray *namen = @[@"Apfel", @"Birne", @"Kirsche"];
      NSArray *z1 = [zahlen filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF BETWEEN {3, 8}"]];
      NSArray *z2 = [zahlen filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF > 4"]];
      NSArray *n1 = [namen filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF == 'Birne'"]];
      NSArray *n2 = [namen filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF CONTAINS 'r'"]];
      check("NSPredicate", [z1 isEqual:@[@4, @7]] && [z2 isEqual:@[@7, @10]] && [n1 isEqual:@[@"Birne"]] &&
                             [n2 isEqual:@[@"Birne", @"Kirsche"]],
            [NSString stringWithFormat:@"%@ %@ %@ %@", z1, z2, n1, n2]);
    });

    TEST("NSThread und performSelector", {
      Faden *f = [Faden new]; f->_cond = [NSCondition new];
      [f->_cond lock];
      [NSThread detachNewThreadSelector:@selector(lauf:) toTarget:f withObject:@"faden"];
      NSDate *bis = [NSDate dateWithTimeIntervalSinceNow:10];
      while (!f->_got && [f->_cond waitUntilDate:bis]) {}
      [f->_cond unlock];
      NSString *p = [ziel performSelector:@selector(text:zahl:) withObject:@"p" withObject:nil];
      check("NSThread und performSelector", [f->_got isEqualToString:@"faden!"] && [p isEqualToString:@"p/0"],
            [NSString stringWithFormat:@"%@ %@", f->_got, p]);
    });

    TEST("NSTimer mit NSInvocation", {
      NSInvocation *inv = [NSInvocation invocationWithMethodSignature:[ziel methodSignatureForSelector:@selector(setZahl:)]];
      int v = 99; inv.target = ziel; inv.selector = @selector(setZahl:); [inv setArgument:&v atIndex:2];
      ziel.zahl = 0;
      NSTimer *t = [NSTimer timerWithTimeInterval:0.05 invocation:inv repeats:NO];
      [[NSRunLoop currentRunLoop] addTimer:t forMode:NSDefaultRunLoopMode];
      NSDate *bis = [NSDate dateWithTimeIntervalSinceNow:5];
      while (ziel.zahl != 99 && [bis timeIntervalSinceNow] > 0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
      check("NSTimer mit NSInvocation", ziel.zahl == 99, [NSString stringWithFormat:@"%d", ziel.zahl]);
    });

    printf("Ergebnis: %d OK, %d Fehler\n", ok, bad);
    return bad != 0;
  }
}
