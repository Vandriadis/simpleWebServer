#define _DARWIN_C_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <CommonCrypto/CommonCrypto.h>

// Simple Base64 encoder
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const unsigned char *input, size_t input_len, char *out) {
    size_t i = 0, o = 0;
    while (i + 2 < input_len) {
        unsigned v = (input[i] << 16) | (input[i + 1] << 8) | input[i + 2];
        out[o++] = base64_table[(v >> 18) & 0x3F];
        out[o++] = base64_table[(v >> 12) & 0x3F];
        out[o++] = base64_table[(v >> 6) & 0x3F];
        out[o++] = base64_table[v & 0x3F];
        i += 3;
    }
    if (i + 1 < input_len) {
        unsigned v = (input[i] << 16);
        out[o++] = base64_table[(v >> 18) & 0x3F];
        out[o++] = base64_table[(v >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (i + 2 == input_len) {
        unsigned v = (input[i] << 16) | (input[i + 1] << 8);
        out[o++] = base64_table[(v >> 18) & 0x3F];
        out[o++] = base64_table[(v >> 12) & 0x3F];
        out[o++] = base64_table[(v >> 6) & 0x3F];
        out[o++] = '=';
    }
    return o;
}

// Compute Sec-WebSocket-Accept = Base64(SHA1(key + GUID))
static void compute_ws_accept(const char *client_key, char *accept_out /*must be >= 29*/) {
    static const char *GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char sha1[CC_SHA1_DIGEST_LENGTH];
    char buf[128];

    size_t n = snprintf(buf, sizeof(buf), "%s%s", client_key, GUID);
    CC_SHA1((const unsigned char *)buf, (CC_LONG)n, sha1);

    char b64[64];
    size_t len = base64_encode(sha1, sizeof(sha1), b64);
    b64[len] = '\0';
    strncpy(accept_out, b64, 64);
}

// Nonblocking helper
static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ---- Minimal HTTP responder for "/" ----
static const char *INDEX_HTML =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=utf-8\r\n"
"Connection: close\r\n"
"\r\n"
"<!doctype html><html><head><meta charset='utf-8'><title>WS Echo</title>"
"<style>body{font-family:ui-monospace,monospace;padding:20px}#log{white-space:pre-wrap;border:1px solid #ccc;padding:10px;height:280px;overflow:auto}</style>"
"</head><body>"
"<h3>WebSocket Echo</h3>"
"<div id='log'></div>"
"<input id='msg' placeholder='Type message'/> <button onclick='sendMsg()'>Send</button>"
"<script>"
"const log = s=>{const d=document.getElementById('log');d.textContent+=s+\"\\n\";d.scrollTop=d.scrollHeight};"
"const ws = new WebSocket('ws://'+location.host+'/ws');"
"ws.onopen=()=>log('[open]');"
"ws.onmessage=(e)=>log('[recv] '+e.data);"
"ws.onclose=()=>log('[close]');"
"function sendMsg(){const v=document.getElementById('msg').value; ws.send(v); log('[send] '+v);}"
"</script>"
"</body></html>";

static const char *BAD_REQUEST =
"HTTP/1.1 400 Bad Request\r\n"
"Connection: close\r\n"
"Content-Length: 0\r\n\r\n";

static const char *NOT_FOUND =
"HTTP/1.1 404 Not Found\r\n"
"Connection: close\r\n"
"Content-Length: 0\r\n\r\n";
