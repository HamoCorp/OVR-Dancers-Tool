#pragma once
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <cstdlib>
#endif

// Returns the OVRDancersTool data directory, creating it if needed.
// Windows: %APPDATA%\OVRDancersTool
// Linux:   $XDG_DATA_HOME/OVRDancersTool  (or ~/.local/share/OVRDancersTool)
inline std::string GetDataDir() {
#ifdef _WIN32
    char appdata[MAX_PATH] = {};
    GetEnvironmentVariableA("APPDATA", appdata, sizeof(appdata));
    std::string dir = std::string(appdata) + "\\OVRDancersTool";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
#else
    std::string dir;
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg && xdg[0]) {
        dir = std::string(xdg) + "/OVRDancersTool";
    } else {
        const char* home = getenv("HOME");
        dir = std::string(home ? home : "/tmp") + "/.local/share/OVRDancersTool";
    }
    mkdir(dir.c_str(), 0755);
    return dir;
#endif
}

namespace BLog {
    inline FILE*& File() { static FILE* f = nullptr; return f; }
    inline std::mutex& Mtx() { static std::mutex m; return m; }

    inline void Init() {
        std::string path = GetDataDir();
#ifdef _WIN32
        path += "\\breakers_log.txt";
        fopen_s(&File(), path.c_str(), "w");
#else
        path += "/breakers_log.txt";
        File() = fopen(path.c_str(), "w");
#endif
        if (File()) fprintf(File(), "=== OVR Dancers Tool log ===\n");
    }

    inline void Write(const char* level, const char* fmt, va_list args) {
        char buf[512];
        vsnprintf(buf, sizeof(buf), fmt, args);
        char line[560];
        snprintf(line, sizeof(line), "[%s] %s\n", level, buf);
        std::lock_guard<std::mutex> lock(Mtx());
#ifdef _WIN32
        OutputDebugStringA(line);
#endif
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
