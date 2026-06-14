#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>

// Simple logger: writes to breakers_log.txt next to the exe + OutputDebugStringA.
// Usage: LOG_INFO("connected to pipe"); LOG_ERR("WriteFile failed: %lu", GetLastError());

namespace BLog {
    inline FILE*& File() { static FILE* f = nullptr; return f; }
    inline std::mutex& Mtx() { static std::mutex m; return m; }

    inline void Init() {
        fopen_s(&File(), "breakers_log.txt", "w");
        if (File()) fprintf(File(), "=== Breakers Tool log ===\n");
    }

    inline void Write(const char* level, const char* fmt, va_list args) {
        char buf[512];
        vsnprintf(buf, sizeof(buf), fmt, args);

        char line[560];
        // Timestamp (seconds since epoch, lightweight)
        snprintf(line, sizeof(line), "[%s] %s\n", level, buf);

        std::lock_guard<std::mutex> lock(Mtx());
        OutputDebugStringA(line);
        if (File()) { fputs(line, File()); fflush(File()); }
    }
}

inline void LOG_INFO(const char* fmt, ...) {
    va_list a; va_start(a, fmt); BLog::Write("INFO", fmt, a); va_end(a);
}
inline void LOG_WARN(const char* fmt, ...) {
    va_list a; va_start(a, fmt); BLog::Write("WARN", fmt, a); va_end(a);
}
inline void LOG_ERR(const char* fmt, ...) {
    va_list a; va_start(a, fmt); BLog::Write("ERR ", fmt, a); va_end(a);
}
