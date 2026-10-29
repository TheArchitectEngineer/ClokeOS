// PureDarwin: OSLogPrintStream gehört zu WebKits Cocoa-Quellen (nicht im Tarball). Hier schreibt er nach stderr.
#include "config.h"
#include <wtf/darwin/OSLogPrintStream.h>
#include <stdio.h>

namespace WTF {

OSLogPrintStream::OSLogPrintStream(os_log_t log, os_log_type_t type)
    : m_log(log)
    , m_logType(type)
{
}

OSLogPrintStream::~OSLogPrintStream() = default;

std::unique_ptr<OSLogPrintStream> OSLogPrintStream::open(const char*, const char*, os_log_type_t type)
{
    return makeUnique<OSLogPrintStream>(nullptr, type);
}

void OSLogPrintStream::vprintf(const char* format, va_list argList)
{
    vfprintf(stderr, format, argList);
}

} // namespace WTF
