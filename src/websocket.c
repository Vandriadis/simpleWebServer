#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include <CommonCrypto/CommonCrypto.h>
#include "base64.h"
#include "websocket.h"

// Compute Sec-WebSocket-Accept = Base64(SHA1(key + GUID))
void compute_ws_accept(const char *client_key, char *accept_out /*must be >= 29*/) {
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

// Send WebSocket text frame (server -> client, unmasked)
int ws_send_text(int fd, const char *msg, size_t len) {
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
int ws_read_and_echo(int fd) {
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

