/* Globale Menüleiste für Darlings AppKit auf ClokeOS: das Hauptmenü des Programms steht wie bei macOS
 * oben in CloakServers Leiste, nicht im Fenster.
 *
 * Protokoll (CloakServer, cloakserver.c, Abschnitt „Globale Menüleiste“):
 *   - Eigenschaft _CLOKE_MENU (UTF8_STRING, Format 8) an jedem Fenster des Programms, eine Zeile je
 *     Eintrag in Baumreihenfolge: "<tiefe>\t<art>\t<nummer>\t<merkmale>\t<text>\t<kürzel>\n"
 *     tiefe 0 = Menü in der Leiste, 1 = dessen Einträge, 2 = Untermenü-Einträge;
 *     art m (Untermenü), i (Eintrag), s (Trennlinie); merkmale d (gesperrt), c (Haken).
 *     Das App-Menü (das erste) fehlt, CloakServer zeigt den Programmnamen selbst.
 *   - Wählt der Benutzer einen Eintrag, kommt eine ClientMessage _CLOKE_MENU_ACTIVATE an das Fenster,
 *     data.l[0] = Nummer.
 *
 * Umsetzung ohne Eingriff in Darlings Quellen:
 *   - +[NSWindow hasMainMenuForStyleMask:] (in Cocotron eigens zum Überschreiben gedacht) liefert NO,
 *     Fenster bekommen also keine Menüzeile (NSMainMenuView) und keine Höhe dafür.
 *   - Ein Beobachter der Haupt-Run-Loop baut vor jedem Warten (höchstens alle 0,2 s) die Zeilen aus
 *     [NSApp mainMenu] samt Validierung (-[NSMenu update]) und setzt die Eigenschaft nur, wenn sie sich
 *     geändert hat oder ein Fenster neu ist.
 *   - -[X11Display postXEvent:] fängt _CLOKE_MENU_ACTIVATE ab. Die Aktion läuft danach über
 *     -[NSRunLoop performSelector:…] (wie WM_DELETE_WINDOW bei Darling), nicht im Rückruf der X-Quelle:
 *     öffnet sie einen modalen Dialog, braucht dessen Run-Loop die X-Quelle wieder.
 *
 * CLOKE_MENU_IM_FENSTER=1 lässt alles wie bei Darling (Menü im Fenster, keine Eigenschaft). */
#import "X11Display.h"
#import "X11Window.h"
#import <AppKit/NSApplication.h>
#import <AppKit/NSMenu.h>
#import <AppKit/NSMenuItem.h>
#import <AppKit/NSWindow.h>
#import <CoreFoundation/CoreFoundation.h>
#import <objc/runtime.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdlib.h>

@interface ClokeMenu : NSObject
+ (void) activate: (NSNumber *) number;
@end

static NSMutableArray *clokeItems;           /* Nummer n -> clokeItems[n - 1] */
static NSMutableString *clokeLast;           /* zuletzt gesetzte Zeilen */
static NSMutableDictionary *clokeWindows;    /* X-Fenster -> Zeilen, die dort stehen */
static CFAbsoluteTime clokeLastRun;
static CFRunLoopTimerRef clokeTimer;
static Atom atomMenu, atomActivate, atomUTF8;
static void (*origPostXEvent)(id, SEL, XEvent *);

/* "⌘S" usw. wie in macOS-Menüs */
static NSString *clokeKey(NSMenuItem *item) {
    NSString *k = [item keyEquivalent];
    if ([k length] == 0)
        return @"";
    NSUInteger mods = [item keyEquivalentModifierMask];
    unichar c = [k characterAtIndex: 0];
    NSMutableString *s = [NSMutableString string];
    if (mods & NSControlKeyMask)
        [s appendString: @"⌃"];
    if (mods & NSAlternateKeyMask)
        [s appendString: @"⌥"];
    if ((mods & NSShiftKeyMask) || (c >= 'A' && c <= 'Z'))
        [s appendString: @"⇧"];
    if (mods & NSCommandKeyMask)
        [s appendString: @"⌘"];
    switch (c) {
    case '\r': case 3: [s appendString: @"↩"]; break;
    case 27: [s appendString: @"⎋"]; break;
    case '\t': [s appendString: @"⇥"]; break;
    case ' ': [s appendString: @"Leertaste"]; break;
    case 8: case 127: [s appendString: @"⌫"]; break;
    case NSDeleteFunctionKey: [s appendString: @"⌦"]; break;
    case NSUpArrowFunctionKey: [s appendString: @"↑"]; break;
    case NSDownArrowFunctionKey: [s appendString: @"↓"]; break;
    case NSLeftArrowFunctionKey: [s appendString: @"←"]; break;
    case NSRightArrowFunctionKey: [s appendString: @"→"]; break;
    default:
        if (c >= NSF1FunctionKey && c <= NSF35FunctionKey)
            [s appendFormat: @"F%d", (int) (c - NSF1FunctionKey + 1)];
        else
            [s appendString: [k uppercaseString]];
    }
    return s;
}

static NSString *clokeText(NSString *t) {
    NSMutableString *s = [NSMutableString stringWithString: t ? t : @""];
    [s replaceOccurrencesOfString: @"\t" withString: @" " options: 0 range: NSMakeRange(0, [s length])];
    [s replaceOccurrencesOfString: @"\n" withString: @" " options: 0 range: NSMakeRange(0, [s length])];
    return s;
}

static void clokeAdd(NSMutableString *out, NSMenu *menu, int depth) {
    for (NSMenuItem *item in [menu itemArray]) {
        if ([item isHidden])
            continue;
        if ([item isSeparatorItem]) {
            if (depth > 0) {
                [clokeItems addObject: item];
                [out appendFormat: @"%d\ts\t%lu\t\t\t\n", depth, (unsigned long) [clokeItems count]];
            }
            continue;
        }
        NSMenu *sub = [item submenu];
        if (depth == 0 && sub == nil)
            continue;   /* in der Leiste stehen nur Menüs */
        [clokeItems addObject: item];
        NSString *flags = [NSString stringWithFormat: @"%@%@", [item isEnabled] ? @"" : @"d",
                                                     [item state] == NSOnState ? @"c" : @""];
        [out appendFormat: @"%d\t%@\t%lu\t%@\t%@\t%@\n", depth, sub ? @"m" : @"i", (unsigned long) [clokeItems count],
                          flags, clokeText([item title]), sub ? @"" : clokeKey(item)];
        if (sub != nil && depth < 2)
            clokeAdd(out, sub, depth + 1);
    }
}

static void clokeUpdate(void) {
    clokeLastRun = CFAbsoluteTimeGetCurrent();
    NSApplication *app = NSApp;
    NSMenu *main = [app mainMenu];
    X11Display *display = (X11Display *) [NSDisplay currentDisplay];
    if (main == nil || ![display isKindOfClass: [X11Display class]])
        return;
    Display *dpy = [display display];
    if (atomMenu == None) {
        atomMenu = XInternAtom(dpy, "_CLOKE_MENU", False);
        atomUTF8 = XInternAtom(dpy, "UTF8_STRING", False);
    }

    [main update];   /* Validierung (validateMenuItem: …), wie vor dem Öffnen eines Menüs */
    NSMutableString *out = [NSMutableString string];
    [clokeItems removeAllObjects];
    NSArray *top = [main itemArray];
    /* Das erste Menü ist das App-Menü (Programmname): CloakServer zeigt es selbst */
    for (NSUInteger i = 1; i < [top count]; i++) {
        NSMenuItem *item = [top objectAtIndex: i];
        if ([item isHidden] || [item submenu] == nil)
            continue;
        [clokeItems addObject: item];
        [out appendFormat: @"0\tm\t%lu\t%@\t%@\t\n", (unsigned long) [clokeItems count],
                          [item isEnabled] ? @"" : @"d", clokeText([item title])];
        clokeAdd(out, [item submenu], 1);
    }
    BOOL changed = ![out isEqualToString: clokeLast];
    if (changed)
        [clokeLast setString: out];

    NSData *bytes = [out dataUsingEncoding: NSUTF8StringEncoding];
    NSMutableDictionary *seen = [NSMutableDictionary dictionary];
    BOOL flush = NO;
    for (NSWindow *w in [app windows]) {
        id pw = [w platformWindow];
        if (![pw isKindOfClass: [X11Window class]])
            continue;
        Window xw = [(X11Window *) pw windowHandle];
        if (xw == None)
            continue;
        NSNumber *key = [NSNumber numberWithUnsignedLong: xw];
        [seen setObject: clokeLast forKey: key];
        if (!changed && [clokeWindows objectForKey: key] != nil)
            continue;
        XChangeProperty(dpy, xw, atomMenu, atomUTF8, 8, PropModeReplace, [bytes bytes], (int) [bytes length]);
        flush = YES;
    }
    [clokeWindows setDictionary: seen];
    if (flush)
        XFlush(dpy);
}

static void clokeTimerFired(CFRunLoopTimerRef t, void *info) {
    @autoreleasepool {
        clokeUpdate();
    }
}

static void clokeObserver(CFRunLoopObserverRef o, CFRunLoopActivity act, void *info) {
    @autoreleasepool {
        CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
        if (now - clokeLastRun >= 0.2) {
            clokeUpdate();
        } else if (CFRunLoopTimerGetNextFireDate(clokeTimer) < now) {
            /* bald noch einmal, damit die letzte Änderung nicht liegen bleibt */
            CFRunLoopTimerSetNextFireDate(clokeTimer, clokeLastRun + 0.21);
        }
    }
}

@implementation ClokeMenu

+ (void) activate: (NSNumber *) number {
    NSUInteger n = [number unsignedIntegerValue];
    if (n == 0 || n > [clokeItems count])
        return;
    NSMenuItem *item = [clokeItems objectAtIndex: n - 1];
    if ([item submenu] != nil || [item isSeparatorItem])
        return;
    [[item menu] update];
    if (![item isEnabled])
        return;
    [NSApp sendAction: [item action] to: [item target] from: item];
    clokeLastRun = 0;   /* Haken und Sperren gleich neu melden */
}

@end

static void clokePostXEvent(id self, SEL _cmd, XEvent *ev) {
    if (ev->type == ClientMessage && ev->xclient.format == 32) {
        if (atomActivate == None)
            atomActivate = XInternAtom(ev->xclient.display, "_CLOKE_MENU_ACTIVATE", False);
        if (ev->xclient.message_type == atomActivate) {
            NSNumber *n = [NSNumber numberWithLong: ev->xclient.data.l[0]];
            [[NSRunLoop currentRunLoop] performSelector: @selector(activate:)
                                                 target: [ClokeMenu class]
                                               argument: n
                                                  order: 0
                                                  modes: @[ NSDefaultRunLoopMode, NSModalPanelRunLoopMode,
                                                            NSEventTrackingRunLoopMode ]];
            return;
        }
    }
    origPostXEvent(self, _cmd, ev);
}

static BOOL clokeNoMenuInWindow(id self, SEL _cmd, NSWindowStyleMask mask) {
    return NO;
}

@implementation X11Display (ClokeMenu)

+ (void) load {
    const char *env = getenv("CLOKE_MENU_IM_FENSTER");
    if (env != NULL && *env && strcmp(env, "0") != 0)
        return;
    clokeItems = [[NSMutableArray alloc] init];
    clokeLast = [[NSMutableString alloc] init];
    clokeWindows = [[NSMutableDictionary alloc] init];

    /* Fenster ohne Menüzeile: NSWindow und alle Unterklassen, die es nicht selbst überschreiben */
    Method m = class_getClassMethod([NSWindow class], @selector(hasMainMenuForStyleMask:));
    if (m != NULL)
        method_setImplementation(m, (IMP) clokeNoMenuInWindow);

    Method p = class_getInstanceMethod(self, @selector(postXEvent:));
    if (p != NULL)
        origPostXEvent = (void (*)(id, SEL, XEvent *)) method_setImplementation(p, (IMP) clokePostXEvent);

    CFRunLoopRef main = CFRunLoopGetMain();
    CFRunLoopObserverRef obs = CFRunLoopObserverCreate(NULL, kCFRunLoopBeforeWaiting, true, 0, clokeObserver, NULL);
    clokeTimer = CFRunLoopTimerCreate(NULL, 1e12, 1e12, 0, 0, clokeTimerFired, NULL);
    CFRunLoopAddObserver(main, obs, kCFRunLoopCommonModes);
    CFRunLoopAddTimer(main, clokeTimer, kCFRunLoopCommonModes);
    for (NSString *mode in @[ NSDefaultRunLoopMode, NSModalPanelRunLoopMode, NSEventTrackingRunLoopMode ]) {
        CFRunLoopAddObserver(main, obs, (CFStringRef) mode);
        CFRunLoopAddTimer(main, clokeTimer, (CFStringRef) mode);
    }
}

@end
