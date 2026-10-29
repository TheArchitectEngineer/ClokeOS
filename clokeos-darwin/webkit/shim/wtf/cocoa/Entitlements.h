/* Im WebKit-Tarball fehlen die Cocoa-Quellen; PureDarwin kennt keine Entitlements. */
#pragma once
#include <wtf/text/ASCIILiteral.h>
#include <string>
namespace WTF {
inline bool processHasEntitlement(const char*) { return false; }
inline bool processHasEntitlement(ASCIILiteral) { return false; }
inline bool hasEntitlement(const char*) { return false; }
}
