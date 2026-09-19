#include "trace.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>
#include <windows.h>

namespace perf::trace
{
namespace
{
std::mutex mutex;
FILE *file = nullptr;
std::atomic<bool> on{false};
} // namespace

void start(const std::filesystem::path &path)
{
    std::lock_guard lock(mutex);
    if (file)
        return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    // Shared for reading, so the trace can be inspected while it is recorded.
    file = _wfsopen(path.c_str(), L"a, ccs=UTF-8", _SH_DENYWR);
    on = file != nullptr;
    if (file)
    {
        fwprintf(file, L"\n---- trace started ----\n");
        fflush(file);
    }
}

void stop()
{
    std::lock_guard lock(mutex);
    on = false;
    if (file)
    {
        fwprintf(file, L"---- trace stopped ----\n");
        fclose(file);
        file = nullptr;
    }
}

bool enabled()
{
    return on;
}

void line(const wchar_t *format, ...)
{
    if (!on)
        return;
    wchar_t text[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text, _TRUNCATE, format, args);
    va_end(args);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard lock(mutex);
    if (!file)
        return;
    // The thread id tells the UI thread's decisions apart from the strip's.
    fwprintf(file, L"%02u:%02u:%02u.%03u [%5lu] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
             GetCurrentThreadId(), text);
    fflush(file);
}
} // namespace perf::trace
