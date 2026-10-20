/* Foundation-Probe: ein Kommandozeilenprogramm wie aus Xcode (x86_64, macOS 11, ARC), das die
 * wichtigsten Teile von Foundation und CoreFoundation prüft. Gebaut mit Apples SDK; unter ClokeOS
 * läuft es gegen Darlings Frameworks, auf dem Mac gegen Apples. Die Ausgabe ist auf beiden Seiten
 * gleich, wenn alles stimmt (keine Zeiten, keine Pfade mit Benutzernamen).
 *
 *   clang -target x86_64-apple-macos11.0 -fobjc-arc -framework Foundation -o fprobe fprobe.m */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int ok, bad;
static void check(const char *name, BOOL cond, NSString *detail) {
  if (cond) { ok++; printf("OK      %s\n", name); }
  else { bad++; printf("FEHLER  %s: %s\n", name, detail ? detail.UTF8String : "?"); }
  fflush(stdout);
}
#define TEST(tname, ...)                                                                  \
  do {                                                                                    \
    printf("...     %s\r", tname); fflush(stdout);                                         \
    @try { __VA_ARGS__ } @catch (NSException *e) {                                               \
      check(tname, NO, [NSString stringWithFormat:@"Ausnahme %@: %@", e.name, e.reason]); } \
  } while (0)

int main(int argc, const char **argv) {
  @autoreleasepool {
    printf("Foundation-Probe 1\n");

    TEST("NSString", {
      NSString *s = [NSString stringWithFormat:@"%@-%d-%.2f-%s", @"ClokeOS", 42, 3.14159, "c"];
      NSString *u = [@"Grüße aus Darwin" uppercaseString];
      check("NSString", [s isEqualToString:@"ClokeOS-42-3.14-c"] && [u isEqualToString:@"GRÜSSE AUS DARWIN"] &&
                        [@"a,b,c" componentsSeparatedByString:@","].count == 3 && [s hasPrefix:@"Cloke"] &&
                        [s rangeOfString:@"42"].location == 8 && [@"  x " stringByTrimmingCharactersInSet:
                            [NSCharacterSet whitespaceCharacterSet]].length == 1,
            [NSString stringWithFormat:@"%@ | %@", s, u]);
    });
    TEST("NSString UTF-8", {
      NSString *s = @"äöü €";
      NSData *d = [s dataUsingEncoding:NSUTF8StringEncoding];
      NSString *back = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding];
      check("NSString UTF-8", d.length == 10 && [back isEqualToString:s] && s.length == 5,
            [NSString stringWithFormat:@"%lu Bytes, %@", (unsigned long)d.length, back]);
    });
    TEST("Literale", {
      NSArray *a = @[ @"x", @2, @3.5 ];
      NSDictionary *d = @{ @"k1" : @"v1", @"k2" : @[ @1, @2 ] };
      NSArray *empty = @[];
      NSDictionary *emptyD = @{};
      check("Literale", a.count == 3 && [a[1] intValue] == 2 && [a[2] doubleValue] == 3.5 &&
                        [d[@"k1"] isEqualToString:@"v1"] && [d[@"k2"] count] == 2 && empty.count == 0 &&
                        emptyD.count == 0 && d[@"fehlt"] == nil && [@42 isEqual:@42] && [@(-7) integerValue] == -7,
            [NSString stringWithFormat:@"%@ %@", a, d]);
    });
    TEST("NSArray", {
      NSMutableArray *m = [NSMutableArray array];
      for (int i = 0; i < 100; i++) [m addObject:@(99 - i)];
      [m sortUsingComparator:^NSComparisonResult(NSNumber *x, NSNumber *y) { return [x compare:y]; }];
      NSArray *f = [m filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"self >= 95"]];
      long sum = 0;
      for (NSNumber *n in m) sum += n.longValue;
      check("NSArray", m.count == 100 && [m[0] intValue] == 0 && [m.lastObject intValue] == 99 && sum == 4950 &&
                       f.count == 5 && [[m valueForKeyPath:@"@max.self"] intValue] == 99,
            [NSString stringWithFormat:@"count %lu sum %ld filter %lu", (unsigned long)m.count, sum, (unsigned long)f.count]);
    });
    TEST("NSDictionary", {
      NSMutableDictionary *m = [NSMutableDictionary dictionary];
      for (int i = 0; i < 50; i++) m[[NSString stringWithFormat:@"k%d", i]] = @(i * i);
      NSArray *keys = [m.allKeys sortedArrayUsingSelector:@selector(compare:)];
      __block long sum = 0;
      [m enumerateKeysAndObjectsUsingBlock:^(id k, NSNumber *v, BOOL *stop) { sum += v.longValue; }];
      check("NSDictionary", m.count == 50 && [m[@"k7"] intValue] == 49 && [keys[0] isEqualToString:@"k0"] && sum == 40425,
            [NSString stringWithFormat:@"count %lu sum %ld", (unsigned long)m.count, sum]);
    });
    TEST("NSSet", {
      NSSet *s = [NSSet setWithArray:@[ @1, @2, @2, @3 ]];
      NSMutableSet *t = [s mutableCopy];
      [t intersectSet:[NSSet setWithObjects:@2, @3, @4, nil]];
      check("NSSet", s.count == 3 && t.count == 2 && [t containsObject:@3], s.description);
    });
    TEST("NSNumber", {
      NSNumber *b = [NSNumber numberWithBool:YES];
      NSNumber *d = [NSNumber numberWithDouble:0.1];
      NSNumber *q = [NSNumber numberWithLongLong:1LL << 40];
      check("NSNumber", b.boolValue && strcmp(q.objCType, "q") == 0 && q.longLongValue == (1LL << 40) &&
                        [d.stringValue isEqualToString:@"0.1"] && [@3 compare:@4] == NSOrderedAscending,
            [NSString stringWithFormat:@"%@ %@ %@", b, d, q]);
    });
    TEST("NSData", {
      NSMutableData *m = [NSMutableData dataWithLength:4];
      ((uint8_t *)m.mutableBytes)[0] = 0xCA;
      [m appendBytes:"\xFE" length:1];
      NSString *b64 = [m base64EncodedStringWithOptions:0];
      NSData *back = [[NSData alloc] initWithBase64EncodedString:b64 options:0];
      check("NSData", m.length == 5 && [back isEqualToData:m] && [b64 isEqualToString:@"ygAAAP4="], b64);
    });
    TEST("NSDate", {
      NSDate *d = [NSDate dateWithTimeIntervalSince1970:1700000000];
      NSDateFormatter *f = [NSDateFormatter new];
      f.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
      f.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:0];
      f.dateFormat = @"yyyy-MM-dd HH:mm:ss";
      NSString *s = [f stringFromDate:d];
      NSDate *p = [f dateFromString:@"2026-10-20 12:00:00"];
      check("NSDate", [s isEqualToString:@"2023-11-14 22:13:20"] && p && (long)p.timeIntervalSince1970 == 1792497600 &&
                      [[NSDate date] timeIntervalSinceDate:d] > 0,
            [NSString stringWithFormat:@"%@ / %@", s, p]);
    });
    TEST("NSCalendar", {
      NSCalendar *c = [[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian];
      c.timeZone = [NSTimeZone timeZoneForSecondsFromGMT:0];
      NSDateComponents *k = [c components:NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay
                                 fromDate:[NSDate dateWithTimeIntervalSince1970:1700000000]];
      check("NSCalendar", k.year == 2023 && k.month == 11 && k.day == 14,
            [NSString stringWithFormat:@"%ld-%ld-%ld", (long)k.year, (long)k.month, (long)k.day]);
    });
    TEST("JSON", {
      NSData *j = [@"{\"name\":\"ClokeOS\",\"zahlen\":[1,2,3],\"ok\":true,\"x\":null}" dataUsingEncoding:NSUTF8StringEncoding];
      NSError *err = nil;
      NSDictionary *o = [NSJSONSerialization JSONObjectWithData:j options:0 error:&err];
      NSData *out = [NSJSONSerialization dataWithJSONObject:@{ @"a" : @[ @1, @"b" ] } options:0 error:&err];
      NSString *outs = [[NSString alloc] initWithData:out encoding:NSUTF8StringEncoding];
      check("JSON", [o[@"name"] isEqualToString:@"ClokeOS"] && [o[@"zahlen"] count] == 3 && [o[@"ok"] boolValue] &&
                    o[@"x"] == [NSNull null] && [outs isEqualToString:@"{\"a\":[1,\"b\"]}"],
            [NSString stringWithFormat:@"%@ | %@ | %@", o, outs, err]);
    });
    TEST("Plist", {
      NSDictionary *p = @{ @"Name" : @"Gast", @"UID" : @501, @"Liste" : @[ @YES, @"zwei" ] };
      NSError *err = nil;
      NSData *xml = [NSPropertyListSerialization dataWithPropertyList:p format:NSPropertyListXMLFormat_v1_0 options:0 error:&err];
      NSData *bin = [NSPropertyListSerialization dataWithPropertyList:p format:NSPropertyListBinaryFormat_v1_0 options:0 error:&err];
      id back1 = [NSPropertyListSerialization propertyListWithData:xml options:0 format:NULL error:&err];
      id back2 = [NSPropertyListSerialization propertyListWithData:bin options:0 format:NULL error:&err];
      check("Plist", [back1 isEqual:p] && [back2 isEqual:p] && memcmp(bin.bytes, "bplist00", 8) == 0,
            [NSString stringWithFormat:@"%@ | %@ | %@", back1, back2, err]);
    });
    TEST("NSKeyedArchiver", {
      NSDictionary *p = @{ @"a" : @[ @1, @"zwei" ], @"b" : [NSDate dateWithTimeIntervalSince1970:0] };
      NSData *d = [NSKeyedArchiver archivedDataWithRootObject:p];
      id back = [NSKeyedUnarchiver unarchiveObjectWithData:d];
      check("NSKeyedArchiver", d.length > 0 && [back isEqual:p], [back description]);
    });
    TEST("NSFileManager", {
      NSFileManager *fm = [NSFileManager defaultManager];
      NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"fprobe-test"];
      [fm removeItemAtPath:dir error:nil];
      NSError *err = nil;
      BOOL made = [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:&err];
      NSString *file = [dir stringByAppendingPathComponent:@"datei.txt"];
      BOOL wrote = [@"Hallo Datei" writeToFile:file atomically:YES encoding:NSUTF8StringEncoding error:&err];
      NSString *read = [NSString stringWithContentsOfFile:file encoding:NSUTF8StringEncoding error:&err];
      NSArray *list = [fm contentsOfDirectoryAtPath:dir error:&err];
      NSDictionary *attr = [fm attributesOfItemAtPath:file error:&err];
      BOOL removed = [fm removeItemAtPath:dir error:&err];
      check("NSFileManager", made && wrote && [read isEqualToString:@"Hallo Datei"] && list.count == 1 &&
                             [attr fileSize] == 11 && removed && ![fm fileExistsAtPath:dir],
            [NSString stringWithFormat:@"%d %d %@ %@ %@", made, wrote, read, list, err]);
    });
    TEST("NSURL", {
      NSURL *u = [NSURL URLWithString:@"https://example.org/pfad/datei.txt?q=1#frag"];
      NSURL *f = [NSURL fileURLWithPath:@"/tmp/a b.txt"];
      NSURLComponents *c = [NSURLComponents componentsWithURL:u resolvingAgainstBaseURL:NO];
      check("NSURL", [u.host isEqualToString:@"example.org"] && [u.lastPathComponent isEqualToString:@"datei.txt"] &&
                     [u.query isEqualToString:@"q=1"] && [f.absoluteString isEqualToString:@"file:///tmp/a%20b.txt"] &&
                     [c.scheme isEqualToString:@"https"],
            [NSString stringWithFormat:@"%@ %@ %@", u.host, f.absoluteString, c.scheme]);
    });
    TEST("NSUserDefaults", {
      NSUserDefaults *ud = [[NSUserDefaults alloc] initWithSuiteName:@"com.clokeos.fprobe"];
      [ud setObject:@"wert" forKey:@"schluessel"];
      [ud setInteger:7 forKey:@"zahl"];
      check("NSUserDefaults", [[ud stringForKey:@"schluessel"] isEqualToString:@"wert"] && [ud integerForKey:@"zahl"] == 7,
            [ud stringForKey:@"schluessel"]);
      [ud removeObjectForKey:@"schluessel"];
      [ud removeObjectForKey:@"zahl"];
    });
    TEST("Toll-free Bridging", {
      CFStringRef cs = CFSTR("Brücke");
      NSString *ns = (__bridge NSString *)cs;
      NSArray *arr = @[ @"a", @"b" ];
      CFArrayRef ca = (__bridge CFArrayRef)arr;
      CFMutableDictionaryRef cd = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
      CFDictionarySetValue(cd, CFSTR("k"), CFSTR("v"));
      NSDictionary *nd = (__bridge_transfer NSDictionary *)cd;
      CFStringRef made = CFStringCreateWithFormat(NULL, NULL, CFSTR("%d-%@"), 5, ns);
      NSString *madeNS = (__bridge_transfer NSString *)made;
      check("Toll-free Bridging", ns.length == 6 && CFStringGetLength(cs) == 6 && CFArrayGetCount(ca) == 2 &&
                                  [nd[@"k"] isEqualToString:@"v"] && [madeNS isEqualToString:@"5-Brücke"] &&
                                  [madeNS isKindOfClass:[NSString class]],
            [NSString stringWithFormat:@"%@ %@ %@", ns, nd, madeNS]);
    });
    TEST("Ausnahmen", {
      BOOL caught = NO;
      NSString *name = nil;
      @try { (void)[@[] objectAtIndex:3]; } @catch (NSException *e) { caught = YES; name = e.name; }
      BOOL caught2 = NO;
      @try { @throw [NSException exceptionWithName:@"Eigene" reason:@"Test" userInfo:nil]; }
      @catch (NSException *e) { caught2 = [e.name isEqualToString:@"Eigene"]; }
      check("Ausnahmen", caught && [name isEqualToString:NSRangeException] && caught2, name);
    });
    TEST("NSRegularExpression", {
      NSRegularExpression *re = [NSRegularExpression regularExpressionWithPattern:@"(\\d+)-(\\w+)" options:0 error:nil];
      NSTextCheckingResult *m = [re firstMatchInString:@"Build 2A17 v20-darwin" options:0 range:NSMakeRange(0, 21)];
      NSString *g = m ? [@"Build 2A17 v20-darwin" substringWithRange:[m rangeAtIndex:2]] : nil;
      check("NSRegularExpression", [g isEqualToString:@"darwin"], g);
    });
    TEST("NSProcessInfo/NSBundle", {
      NSProcessInfo *pi = [NSProcessInfo processInfo];
      check("NSProcessInfo/NSBundle", pi.processIdentifier > 0 && pi.arguments.count >= 1 &&
                                      [pi.processName isEqualToString:@"fprobe"] && [NSBundle mainBundle] != nil &&
                                      pi.environment[@"PATH"] != nil,
            [NSString stringWithFormat:@"%@ %@", pi.processName, pi.arguments]);
    });
    TEST("NSThread/GCD", {
      __block int counter = 0;
      dispatch_group_t g = dispatch_group_create();
      dispatch_queue_t q = dispatch_queue_create("com.clokeos.fprobe", DISPATCH_QUEUE_CONCURRENT);
      NSLock *lock = [NSLock new];
      for (int i = 0; i < 20; i++)
        dispatch_group_async(g, q, ^{ [lock lock]; counter++; [lock unlock]; });
      long waited = dispatch_group_wait(g, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
      __block BOOL threadRan = NO;
      NSThread *t = [[NSThread alloc] initWithBlock:^{ threadRan = YES; }];
      [t start];
      for (int i = 0; i < 200 && !threadRan; i++) usleep(10000);
      check("NSThread/GCD", waited == 0 && counter == 20 && threadRan,
            [NSString stringWithFormat:@"counter %d thread %d", counter, threadRan]);
    });
    TEST("NSRunLoop/NSTimer", {
      __block int fired = 0;
      __block BOOL mainQueue = NO;
      NSTimer *t = [NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) {
        if (++fired == 3) [timer invalidate];
      }];
      [[NSRunLoop currentRunLoop] addTimer:t forMode:NSDefaultRunLoopMode];
      dispatch_async(dispatch_get_main_queue(), ^{ mainQueue = YES; });
      NSDate *until = [NSDate dateWithTimeIntervalSinceNow:3];
      while ((fired < 3 || !mainQueue) && [until timeIntervalSinceNow] > 0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
      check("NSRunLoop/NSTimer", fired == 3 && mainQueue, [NSString stringWithFormat:@"fired %d main %d", fired, mainQueue]);
    });
    TEST("NSNotificationCenter", {
      __block NSString *got = nil;
      id obs = [[NSNotificationCenter defaultCenter] addObserverForName:@"ClokeTest" object:nil queue:nil
                                                             usingBlock:^(NSNotification *n) { got = n.userInfo[@"x"]; }];
      [[NSNotificationCenter defaultCenter] postNotificationName:@"ClokeTest" object:nil userInfo:@{ @"x" : @"y" }];
      [[NSNotificationCenter defaultCenter] removeObserver:obs];
      check("NSNotificationCenter", [got isEqualToString:@"y"], got);
    });
    TEST("KVC/KVO", {
      NSMutableDictionary *m = [@{ @"a" : @{ @"b" : @5 } } mutableCopy];
      check("KVC/KVO", [[m valueForKeyPath:@"a.b"] intValue] == 5, [[m valueForKeyPath:@"a.b"] description]);
    });
    TEST("NSLog", {
      NSLog(@"NSLog aus der Foundation-Probe (%@)", @"stderr");
      check("NSLog", YES, nil);
    });

    printf("Ergebnis: %d OK, %d FEHLER\n", ok, bad);
    return bad ? 1 : 0;
  }
}
