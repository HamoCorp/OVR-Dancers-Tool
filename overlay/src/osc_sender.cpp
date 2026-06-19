// Must come before any header that includes windows.h
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "osc_sender.h"
#include <cstring>
#include <cstdio>

static SOCKET g_oscSock = INVALID_SOCKET;

void OSCInit() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_oscSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

void OSCShutdown() {
    if (g_oscSock != INVALID_SOCKET) {
        closesocket(g_oscSock);
        g_oscSock = INVALID_SOCKET;
    }
    WSACleanup();
}

void OSCSendBool(const OSCSettings& s, const char* paramName, bool value) {
    if (!s.enabled || g_oscSock == INVALID_SOCKET) return;

    // Build full VRChat/Resonite avatar parameter path
    char address[128];
    snprintf(address, sizeof(address), "/avatar/parameters/%s", paramName);

    // Build OSC message (all strings are null-terminated + padded to 4-byte boundary)
    uint8_t buf[256] = {};
    int pos = 0;

    // Address string
    int addrLen = (int)strlen(address) + 1;
    memcpy(buf + pos, address, addrLen);
    pos += addrLen;
    pos = (pos + 3) & ~3;

    // Type tag string ",i" (int32)
    buf[pos] = ','; buf[pos + 1] = 'i'; buf[pos + 2] = 0;
    pos += 3;
    pos = (pos + 3) & ~3;

    // Big-endian int32 value (0 = false, 1 = true)
    int32_t v = value ? 1 : 0;
    buf[pos]     = (uint8_t)((v >> 24) & 0xFF);
    buf[pos + 1] = (uint8_t)((v >> 16) & 0xFF);
    buf[pos + 2] = (uint8_t)((v >> 8)  & 0xFF);
    buf[pos + 3] = (uint8_t)(v & 0xFF);
    pos += 4;

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port   = htons(s.port);
    inet_pton(AF_INET, s.ip, &dest.sin_addr);

    sendto(g_oscSock, (const char*)buf, pos, 0, (sockaddr*)&dest, sizeof(dest));
}
