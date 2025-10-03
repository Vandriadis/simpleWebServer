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
    if (i + 1 == input_len) {
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

// Read one WebSocket frame (client -> server, masked). Returns -1 error/closed, 0 if not a full frame yet, 1 if OK
static int ws_read_and_echo(int fd) {
    unsigned char hdr[2];
    ssize_t n = recv(fd, hdr, 2, MSG_PEEK);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; // no data yet
        return -1;
    }
    if (n == 0) return -1;
    if (n < 2)  return 0;

    unsigned fin = (hdr[0] & 0x80) != 0;
    unsigned opcode = hdr[0] & 0x0F;
    unsigned masked = (hdr[1] & 0x80) != 0;
    uint64_t plen = hdr[1] & 0x7F;
    size_t header_len = 2;

    if (plen == 126) header_len += 2;
    else if (plen == 127) header_len += 8;
    if (masked) header_len += 4;

    // ensure full header available
    unsigned char header[14];
    n = recv(fd, header, header_len, MSG_PEEK);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    if (n < (ssize_t)header_len) return 0;

    size_t off = 2;
    if (plen == 126) {
        plen = ((uint64_t)header[2] << 8) | header[3];
        off += 2;
    } else if (plen == 127) {
        plen = 0;
        for (int i = 0; i < 8; ++i) plen = (plen << 8) | header[2+i];
        off += 8;
    }

    unsigned char mask[4] = {0};
    if (masked) {
        mask[0] = header[off+0];
        mask[1] = header[off+1];
        mask[2] = header[off+2];
        mask[3] = header[off+3];
    }

    size_t total = header_len + plen;
    unsigned char *frame = (unsigned char*)malloc(total);
    if (!frame) return -1;

    ssize_t got = recv(fd, frame, total, 0);
    if (got < 0) {
        free(frame);
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
    if (got == 0) { free(frame); return -1; }
    if ((size_t)got < total) {
        free(frame);
        return 0;
    }

    unsigned char *payload = frame + header_len;

    if (masked) {
        for (uint64_t i = 0; i < plen; ++i) payload[i] ^= mask[i % 4];
    }

    if (opcode == 0x8) { // close
        free(frame);
        return -1;
    } else if (opcode == 0x1) { // text
        // tiny log to stdout
        fwrite("[ws recv] ", 1, 10, stdout);
        fwrite(payload, 1, (size_t)plen, stdout);
        fwrite("\n", 1, 1, stdout);
        // Removed upgrade log here as per instructions

        ws_send_text(fd, (const char*)payload, (size_t)plen);
    } else if (opcode == 0x9) {
        unsigned char hdr2[10];
        size_t hlen = 0;
        hdr2[0] = 0x80 | 0xA;
        if (plen < 126) { hdr2[1] = (unsigned char)plen; hlen = 2; }
        else if (plen <= 0xFFFF) { hdr2[1]=126; hdr2[2]=(plen>>8)&0xFF; hdr2[3]=plen&0xFF; hlen=4; }
        else { hdr2[1]=127; for (int i=0;i<8;i++) hdr2[2+i]=(plen>>(56-8*i))&0xFF; hlen=10; }
        write(fd, hdr2, hlen);
        if (plen) write(fd, payload, (size_t)plen);
    }

    free(frame);
    (void)fin;
    return 1;
}

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int _) { (void)_; g_stop = 1; }

int main(void) {
    signal(SIGINT, on_sigint);
    signal(SIGPIPE, SIG_IGN); // prevent SIGPIPE on write to closed socket

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int opt = 1;
    if (setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) { perror("setsockopt"); return 1; }

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_NOSIGPIPE
    setsockopt(srv, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    struct sockaddr_in addr; bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(srv, 16) < 0) { perror("listen"); return 1; }
    set_nonblock(srv);

    printf("Listening on http://127.0.0.1:8080  (Ctrl+C to stop)\n");

    // Simple connection table
    typedef enum { CONN_HTTP=0, CONN_WS=1 } ConnType;
    typedef struct { int fd; ConnType type; } Conn;
    Conn conns[FD_SETSIZE];
    for (int i = 0; i < FD_SETSIZE; i++) conns[i].fd = -1;

    fd_set rfds;
    int maxfd = srv;

    while (!g_stop) {
        FD_ZERO(&rfds);
        FD_SET(srv, &rfds);
        maxfd = srv;

        for (int i = 0; i < FD_SETSIZE; i++) if (conns[i].fd >= 0) {
            FD_SET(conns[i].fd, &rfds);
            if (conns[i].fd > maxfd) maxfd = conns[i].fd;
        }

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0 ) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (FD_ISSET(srv, &rfds)) {
            int cfd = accept(srv, NULL, NULL);
            if (cfd >= 0) {
                set_nonblock(cfd);
#ifdef SO_NOSIGPIPE
                int one = 1;
                setsockopt(cfd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
                int placed = 0;
                for (int i = 0; i < FD_SETSIZE; i++) if (conns[i].fd < 0) {
                    conns[i].fd = cfd; conns[i].type = CONN_HTTP; placed = 1; break;
                }
                if (!placed) close(cfd);
            }
        }
        
        for (int i = 0; i < FD_SETSIZE; i++) if (conns[i].fd >= 0 && FD_ISSET(conns[i].fd, &rfds)) {
            int fd = conns[i].fd;
            if (conns[i].type == CONN_WS) {
                int r = ws_read_and_echo(fd);
                if (r <= 0) { close(fd); conns[i].fd = -1; }
                continue; 
            }

            // Read HTTP request
            char buf[8192];
            ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
            if (n <= 0) { close(fd); conns[i].fd = -1; continue; }
            buf[n] = '\0';

           if (!strstr(buf, "\r\n\r\n")) { 
            send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
            close(fd); conns[i].fd = -1; continue;
           }

           char *method=NULL, *path=NULL, *ws_key=NULL;
           if (parse_http_request(buf, &method, &path, &ws_key) < 0) {
            send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
            close(fd); conns[i].fd = -1; continue;
           }
           
           if (strcasecmp(method, "GET") != 0) {
            send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
            close(fd); conns[i].fd = -1; continue;
           }
           
           if (strcasecmp(path, "/") == 0) {
            send(fd, INDEX_HTML, strlen(INDEX_HTML), 0);
            close(fd); conns[i].fd = -1; continue;
           }

           if (strcmp(path, "/ws") == 0 && ws_key) {
            char accept[64]; compute_ws_accept(ws_key, accept);
            char resp[512];
            int m = snprintf(resp, sizeof(resp),
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Connection: Upgrade\r\n"
                "Upgrade: websocket\r\n"
                "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
            send(fd, resp, (size_t)m, 0);
            printf("[upgrade] client fd=%d -> WebSocket\n", fd);
            fflush(stdout);
            conns[i].type = CONN_WS;
            continue;
           }
           
           // Unknown path
           send(fd, NOT_FOUND, strlen(NOT_FOUND), 0);
           close(fd); conns[i].fd = -1;
        }

    }
    for (int i = 0; i < FD_SETSIZE; i++) if (conns[i].fd >= 0) close(conns[i].fd);
    close(srv);
    printf("Server stopped\n");
    return 0;
}