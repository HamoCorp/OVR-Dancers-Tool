// Must be before any header that includes windows.h
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "websocket_server.h"
#include <windows.h>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdio>

// ── SHA-1 ────────────────────────────────────────────────────────────────────

struct SHA1State { uint32_t h[5]; uint64_t len; uint8_t buf[64]; int bufLen; };

static void sha1_init(SHA1State& s) {
    s.h[0]=0x67452301; s.h[1]=0xEFCDAB89; s.h[2]=0x98BADCFE;
    s.h[3]=0x10325476; s.h[4]=0xC3D2E1F0;
    s.len=0; s.bufLen=0;
    memset(s.buf, 0, sizeof(s.buf));
}
static uint32_t rotl32(uint32_t v, int n) { return (v<<n)|(v>>(32-n)); }
static void sha1_block(SHA1State& s, const uint8_t* block) {
    uint32_t w[80];
    for (int i=0;i<16;i++)
        w[i]=((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|
             ((uint32_t)block[i*4+2]<<8)|block[i*4+3];
    for (int i=16;i<80;i++) w[i]=rotl32(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=s.h[0],b=s.h[1],c=s.h[2],d=s.h[3],e=s.h[4];
    for (int i=0;i<80;i++) {
        uint32_t f,k;
        if(i<20){f=(b&c)|(~b&d);k=0x5A827999;}
        else if(i<40){f=b^c^d;k=0x6ED9EBA1;}
        else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
        else{f=b^c^d;k=0xCA62C1D6;}
        uint32_t t=rotl32(a,5)+f+e+k+w[i]; e=d;d=c;c=rotl32(b,30);b=a;a=t;
    }
    s.h[0]+=a;s.h[1]+=b;s.h[2]+=c;s.h[3]+=d;s.h[4]+=e;
}
static void sha1_update(SHA1State& s, const uint8_t* data, int len) {
    for (int i=0;i<len;i++) {
        s.buf[s.bufLen++]=data[i]; s.len++;
        if (s.bufLen==64) { sha1_block(s,s.buf); s.bufLen=0; }
    }
}
static void sha1_final(SHA1State& s, uint8_t out[20]) {
    uint64_t bits=s.len*8;
    uint8_t pad=0x80; sha1_update(s,&pad,1);
    while (s.bufLen!=56) { pad=0; sha1_update(s,&pad,1); }
    for (int i=7;i>=0;i--) { pad=(uint8_t)(bits>>(i*8)); sha1_update(s,&pad,1); }
    for (int i=0;i<5;i++) {
        out[i*4]=(uint8_t)(s.h[i]>>24); out[i*4+1]=(uint8_t)(s.h[i]>>16);
        out[i*4+2]=(uint8_t)(s.h[i]>>8); out[i*4+3]=(uint8_t)(s.h[i]);
    }
}

// ── Base64 ───────────────────────────────────────────────────────────────────

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const uint8_t* in, int len) {
    std::string out;
    for (int i=0;i<len;i+=3) {
        uint32_t b=(uint32_t)in[i]<<16;
        if (i+1<len) b|=(uint32_t)in[i+1]<<8;
        if (i+2<len) b|=in[i+2];
        out+=kB64[(b>>18)&63]; out+=kB64[(b>>12)&63];
        out+=(i+1<len)?kB64[(b>>6)&63]:'=';
        out+=(i+2<len)?kB64[b&63]:'=';
    }
    return out;
}

// ── WebSocket Handshake ───────────────────────────────────────────────────────

static std::string wsAcceptKey(const std::string& clientKey) {
    const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string cat = clientKey + magic;
    SHA1State sha; sha1_init(sha);
    sha1_update(sha, (const uint8_t*)cat.c_str(), (int)cat.size());
    uint8_t hash[20]; sha1_final(sha, hash);
    return base64_encode(hash, 20);
}

// ── Server State ──────────────────────────────────────────────────────────────

static std::atomic<bool> g_wsRunning{false};
static std::thread       g_wsThread;
static std::mutex        g_clientsMutex;
static std::vector<SOCKET> g_clients;
static SOCKET            g_serverSock = INVALID_SOCKET;

static void handleClient(SOCKET sock) {
    // Read HTTP upgrade request
    char buf[4096] = {};
    int n = recv(sock, buf, sizeof(buf)-1, 0);
    if (n <= 0) { closesocket(sock); return; }
    buf[n] = 0;

    std::string req(buf);
    const char* keyHdr = "Sec-WebSocket-Key: ";
    size_t p = req.find(keyHdr);
    if (p == std::string::npos) { closesocket(sock); return; }
    p += strlen(keyHdr);
    size_t e = req.find("\r\n", p);
    if (e == std::string::npos) { closesocket(sock); return; }
    std::string key = req.substr(p, e-p);
    // Trim any whitespace
    while (!key.empty() && (key.back()=='\r'||key.back()=='\n'||key.back()==' ')) key.pop_back();

    std::string accept = wsAcceptKey(key);
    char resp[512];
    snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept.c_str());
    send(sock, resp, (int)strlen(resp), 0);

    {
        std::lock_guard<std::mutex> lg(g_clientsMutex);
        g_clients.push_back(sock);
    }

    // Drain incoming frames to keep connection alive (we don't need to process them)
    while (g_wsRunning) {
        char tmp[256];
        int r = recv(sock, tmp, sizeof(tmp), 0);
        if (r <= 0) break;
    }

    {
        std::lock_guard<std::mutex> lg(g_clientsMutex);
        for (auto it = g_clients.begin(); it != g_clients.end(); ++it)
            if (*it == sock) { g_clients.erase(it); break; }
    }
    closesocket(sock);
}

static void serverThread(uint16_t port) {
    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) { g_wsRunning = false; return; }
    g_serverSock = srv;

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    // Non-blocking accept loop so we can check g_wsRunning
    u_long nb = 1;
    ioctlsocket(srv, FIONBIO, &nb);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(srv, 4) != 0) {
        closesocket(srv);
        g_serverSock = INVALID_SOCKET;
        g_wsRunning  = false;
        return;
    }

    while (g_wsRunning) {
        SOCKET client = accept(srv, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (!g_wsRunning) break;
            Sleep(10);
            continue;
        }
        // Switch client back to blocking
        u_long bl = 0;
        ioctlsocket(client, FIONBIO, &bl);
        std::thread(handleClient, client).detach();
    }
    // g_serverSock already closed by WSShutdown
}

// ── Public API ────────────────────────────────────────────────────────────────

void WSInit(const WSSettings& s) {
    if (!s.enabled || g_wsRunning) return;
    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    g_wsRunning = true;
    g_wsThread  = std::thread(serverThread, s.port);
}

void WSShutdown() {
    if (!g_wsRunning) return;
    g_wsRunning = false;
    SOCKET srv = g_serverSock;
    g_serverSock = INVALID_SOCKET;
    if (srv != INVALID_SOCKET) closesocket(srv); // wakes accept()
    if (g_wsThread.joinable()) g_wsThread.join();
    {
        std::lock_guard<std::mutex> lg(g_clientsMutex);
        for (auto c : g_clients) closesocket(c);
        g_clients.clear();
    }
    WSACleanup();
}

void WSBroadcast(const char* paramName, bool value) {
    char msg[256];
    snprintf(msg, sizeof(msg), "{\"param\":\"%s\",\"value\":%d}", paramName, value ? 1 : 0);
    int msgLen = (int)strlen(msg);

    // WebSocket text frame header
    uint8_t frame[270];
    frame[0] = 0x81; // FIN + opcode=text
    int hdrLen;
    if (msgLen < 126) {
        frame[1] = (uint8_t)msgLen;
        hdrLen = 2;
    } else {
        frame[1] = 126;
        frame[2] = (uint8_t)(msgLen >> 8);
        frame[3] = (uint8_t)(msgLen & 0xFF);
        hdrLen = 4;
    }
    memcpy(frame + hdrLen, msg, msgLen);
    int total = hdrLen + msgLen;

    std::lock_guard<std::mutex> lg(g_clientsMutex);
    for (SOCKET c : g_clients)
        send(c, (const char*)frame, total, 0);
}

void WSUpdateSettings(const WSSettings& s) {
    WSShutdown();
    if (s.enabled) WSInit(s);
}
