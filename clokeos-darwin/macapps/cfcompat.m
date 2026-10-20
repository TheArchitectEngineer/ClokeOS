/* Ergänzungen für Darlings CoreFoundation auf ClokeOS: Literal-Objekte, die Apples clang erzeugt.
 *
 * Apples clang übersetzt Objective-C-Literale ab macOS 11 als Ziel in feste Objekte im Programm:
 *   @[]          -> ___NSArray0__struct          (Klasse __NSArray0)
 *   @{}          -> ___NSDictionary0__struct     (Klasse __NSDictionary0)
 *   @[a, b]      -> { NSConstantArray, count, objects }
 *   @{k: v, …}   -> { NSConstantDictionary, options, count, keys, objects }
 * Apple legt diese Klassen in CoreFoundation, Darling hat sie nicht. Ohne sie lädt kein Programm,
 * das mit aktuellem Xcode für macOS 11+ gebaut ist und solche Literale benutzt. Die Layouts sind
 * aus clangs Ausgabe abgelesen (Abschnitte __objc_arrayobj, __objc_dictobj). Die Zahlen-Literale
 * (NSConstantIntegerNumber usw.) gehören zu Foundation, siehe fndcompat.m. */
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSException.h>

/* Objekte ohne Referenzzähler: sie liegen im Programm und leben für immer. */
#define CLOKE_IMMORTAL                                        \
  -(id)retain { return self; }                                \
  -(oneway void)release {}                                    \
  -(id)autorelease { return self; }                           \
  -(NSUInteger)retainCount { return NSUIntegerMax; }          \
  -(id)copyWithZone:(NSZone *)zone { return self; }           \
  -(void)dealloc { if (0) [super dealloc]; }

@interface __NSArray0 : NSArray
@end
@interface __NSDictionary0 : NSDictionary
@end
@interface NSConstantArray : NSArray {
  NSUInteger _count;
  const id *_objects;
}
@end
@interface NSConstantDictionary : NSDictionary {
  NSUInteger _options;
  NSUInteger _count;
  const id *_keys;
  const id *_objects;
}
@end

extern char cloke_NSArray0_class __asm("_OBJC_CLASS_$___NSArray0");
extern char cloke_NSDictionary0_class __asm("_OBJC_CLASS_$___NSDictionary0");
__attribute__((used)) struct { void *isa; } __NSArray0__struct = { &cloke_NSArray0_class };
__attribute__((used)) struct { void *isa; } __NSDictionary0__struct = { &cloke_NSDictionary0_class };

static NSUInteger enumerate(NSFastEnumerationState *state, const id *items, NSUInteger count, void *owner) {
  if (state->state != 0) return 0;
  state->state = 1;
  state->itemsPtr = (__unsafe_unretained id *)(void *)items;
  state->mutationsPtr = (unsigned long *)owner; /* unveränderlich: zeigt auf etwas Festes */
  return count;
}

@implementation __NSArray0
CLOKE_IMMORTAL
- (NSUInteger)count { return 0; }
- (id)objectAtIndex:(NSUInteger)index {
  [NSException raise:NSRangeException format:@"*** -[__NSArray0 objectAtIndex:]: index %lu beyond bounds for empty array",
                                              (unsigned long)index];
  return nil;
}
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state objects:(id *)buffer count:(NSUInteger)len {
  return 0;
}
@end

@implementation __NSDictionary0
CLOKE_IMMORTAL
- (NSUInteger)count { return 0; }
- (id)objectForKey:(id)key { return nil; }
- (NSEnumerator *)keyEnumerator { return [(NSArray *)(void *)&__NSArray0__struct objectEnumerator]; }
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state objects:(id *)buffer count:(NSUInteger)len {
  return 0;
}
@end

@implementation NSConstantArray
CLOKE_IMMORTAL
- (NSUInteger)count { return _count; }
- (id)objectAtIndex:(NSUInteger)index {
  if (index >= _count)
    [NSException raise:NSRangeException format:@"*** -[NSConstantArray objectAtIndex:]: index %lu beyond bounds [0 .. %lu]",
                                                (unsigned long)index, (unsigned long)_count - 1];
  return _objects[index];
}
- (void)getObjects:(id *)objects range:(NSRange)range {
  for (NSUInteger i = 0; i < range.length; i++) objects[i] = [self objectAtIndex:range.location + i];
}
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state objects:(id *)buffer count:(NSUInteger)len {
  return enumerate(state, _objects, _count, &_count);
}
@end

@implementation NSConstantDictionary
CLOKE_IMMORTAL
- (NSUInteger)count { return _count; }
- (id)objectForKey:(id)key {
  if (!key) return nil;
  for (NSUInteger i = 0; i < _count; i++)
    if (_keys[i] == key || [_keys[i] isEqual:key]) return _objects[i];
  return nil;
}
- (NSEnumerator *)keyEnumerator {
  return [[NSArray arrayWithObjects:(id *)_keys count:_count] objectEnumerator];
}
- (NSEnumerator *)objectEnumerator {
  return [[NSArray arrayWithObjects:(id *)_objects count:_count] objectEnumerator];
}
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state objects:(id *)buffer count:(NSUInteger)len {
  return enumerate(state, _keys, _count, &_count);
}
@end
