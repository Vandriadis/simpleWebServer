#include "base64.h"

// Simple Base64 encoder
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t base64_encode(const unsigned char *input, size_t input_len, char *out) {
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

