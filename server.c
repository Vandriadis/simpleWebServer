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

// Small HTTP header parser to get Sec-WebSocket-Key
static int parse_http_request(char *req, char **method, char **path, char **ws_key) {
    *method = strtok(req, " \t\r\n");
    *path = strtok(NULL, " \t\r\n");
    if (!*method || !*path) return -1;

    *ws_key = NULL;
    char *line = NULL;
    while ((line = strtok(NULL, "\r\n"))) {
        if (strncasecmp(line, "Sec-WebSocket-Key:", 18) == 0) {
            char *p = line + 18;
            while (*p == ' ' || *p == '\t') p++;
            *ws_key = p;
        }
    }
    return 0;
}

// Send WebSocket text frame (server -> client, unmasked)
static int ws_send_text(int fd, const char *msg, size_t len) {
    unsigned char hdr[10];
    size_t hlen = 0;

    hdr[0] = 0x80 | 0x1;
    if (len < 126) {
        hdr[1] = (unsigned char)len;
        hlen = 2;
    } else if (len <= 0xFFFF) {
        hdr[1] = 126;
        hdr[2] = (len >> 8) & 0xFF;
        hdr[3] = len & 0xFF;
        hlen = 4;
    } else {
        hdr[1] = 127;
        for (int i = 0; i < 8; i++) hdr[2 + i] = (len >> (8 * (7 - i))) & 0xFF;
        hlen = 10;
    }

    if (write(fd, hdr, hlen) < 0) return -1;
    if (write(fd, msg, len) < 0) return -1;
    return 0;
}