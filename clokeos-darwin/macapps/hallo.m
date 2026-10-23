/* AppKit-Probe: eine kleine Cocoa-App wie aus Xcode (universal, macOS 11, ARC) mit Menü, Fenster,
 * Textfeld, Knopf und Beschriftung. Gebaut mit Apples SDK; unter ClokeOS läuft sie gegen Darlings
 * AppKit aus macapps/appkit.sh (DYLD_FRAMEWORK_PATH=/Library/ClokeOS/MacFrameworks), auf dem Mac
 * gegen Apples.
 *
 *   clang -arch x86_64 -arch arm64 -mmacosx-version-min=11.0 -fobjc-arc -framework Cocoa -o hallo hallo.m
 *
 * Auf stdout meldet sie jeden Schritt ("hallo: …"), damit sich ein Absturz zuordnen lässt.
 *   --klick N     drückt nach N Sekunden den Knopf (performClick:), die Beschriftung ändert sich
 *   --ende N      beendet sich nach N Sekunden
 * Benutzt nur alte AppKit-API (bis etwa macOS 10.10), die auch Darling kennt. */
#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void step(const char *msg) {
    printf("hallo: %s\n", msg);
    fflush(stdout);
}

@interface HalloDelegate : NSObject <NSApplicationDelegate>
@property(strong) NSWindow *window;
@property(strong) NSTextField *field;
@property(strong) NSTextField *label;
@property(strong) NSButton *button;
@property double klick, ende;
@end

@implementation HalloDelegate

- (NSMenu *)mainMenu {
    NSMenu *bar = [[NSMenu alloc] initWithTitle:@"Hauptmenü"];

    NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@"Hallo" action:NULL keyEquivalent:@""];
    NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Hallo"];
    [appMenu addItemWithTitle:@"Über Hallo" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Hallo beenden" action:@selector(terminate:) keyEquivalent:@"q"];
    [appItem setSubmenu:appMenu];
    [bar addItem:appItem];

    NSMenuItem *fileItem = [[NSMenuItem alloc] initWithTitle:@"Ablage" action:NULL keyEquivalent:@""];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"Ablage"];
    [fileMenu addItemWithTitle:@"Hallo sagen" action:@selector(sayHello:) keyEquivalent:@"h"];
    [fileMenu addItemWithTitle:@"Fenster schließen" action:@selector(performClose:) keyEquivalent:@"w"];
    [fileItem setSubmenu:fileMenu];
    [bar addItem:fileItem];
    return bar;
}

static NSTextField *makeLabel(NSRect frame, NSString *text) {
    NSTextField *t = [[NSTextField alloc] initWithFrame:frame];
    [t setStringValue:text];
    [t setEditable:NO];
    [t setSelectable:NO];
    [t setBordered:NO];
    [t setDrawsBackground:NO];
    return t;
}

- (void)applicationDidFinishLaunching:(NSNotification *)note {
    step("applicationDidFinishLaunching");
    [NSApp setMainMenu:[self mainMenu]];
    step("Menü gesetzt");

    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
                       NSWindowStyleMaskResizable;
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(220, 260, 440, 210)
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    [self.window setTitle:@"Hallo ClokeOS"];
    [self.window setReleasedWhenClosed:NO];
    step("Fenster angelegt");

    NSView *content = [self.window contentView];
    [content addSubview:makeLabel(NSMakeRect(20, 160, 400, 24),
                                  @"Eine Cocoa-App aus Apples SDK, mit Darlings AppKit.")];

    self.field = [[NSTextField alloc] initWithFrame:NSMakeRect(20, 116, 290, 26)];
    [self.field setStringValue:@"ClokeOS"];
    [content addSubview:self.field];

    self.button = [[NSButton alloc] initWithFrame:NSMakeRect(320, 112, 100, 34)];
    [self.button setTitle:@"Hallo sagen"];
    [self.button setButtonType:NSButtonTypeMomentaryPushIn];
    [self.button setBezelStyle:NSBezelStyleRounded];
    [self.button setTarget:self];
    [self.button setAction:@selector(sayHello:)];
    [self.button setKeyEquivalent:@"\r"];
    [content addSubview:self.button];

    self.label = makeLabel(NSMakeRect(20, 60, 400, 30), @"Noch niemand begrüßt.");
    [self.label setFont:[NSFont boldSystemFontOfSize:16]];
    [content addSubview:self.label];
    step("Inhalt angelegt");

    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:self.field];
    [NSApp activateIgnoringOtherApps:YES];
    step("Fenster gezeigt");

    if (self.klick > 0)
        [self performSelector:@selector(autoClick) withObject:nil afterDelay:self.klick];
    if (self.ende > 0)
        [NSApp performSelector:@selector(terminate:) withObject:nil afterDelay:self.ende];
}

- (void)autoClick {
    step("klicke Knopf");
    [self.button performClick:nil];
}

- (void)sayHello:(id)sender {
    NSString *name = [self.field stringValue];
    if ([name length] == 0)
        name = @"Welt";
    [self.label setStringValue:[NSString stringWithFormat:@"Hallo, %@!", name]];
    printf("hallo: Knopf gedrückt, Beschriftung \"%s\"\n", [[self.label stringValue] UTF8String]);
    fflush(stdout);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    return YES;
}

- (void)applicationWillTerminate:(NSNotification *)note {
    step("Ende");
}

@end

int main(int argc, const char **argv) {
    @autoreleasepool {
        step("Start");
        HalloDelegate *delegate = [HalloDelegate new];
        for (int i = 1; i + 1 < argc; i++) {
            if (strcmp(argv[i], "--klick") == 0)
                delegate.klick = atof(argv[++i]);
            else if (strcmp(argv[i], "--ende") == 0)
                delegate.ende = atof(argv[++i]);
        }
        NSApplication *app = [NSApplication sharedApplication];
        step("NSApplication da");
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app setDelegate:delegate];
        step("Lauf");
        [app run];
    }
    return 0;
}
