#include "auth.h"
#include "base64.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CommonCrypto/CommonCrypto.h>
#include <CommonCrypto/CommonRandom.h>
#else
/* For Linux, link with OpenSSL:
   -DOPENSSL and add -lcrypto, then replace CCKeyDerivationPBKDF with PKCS5_PBKDF2_HMAC
   and CCRandomGenerateBytes with RAND_bytes.
*/
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

static int b64url_from_bytes(const unsigned char *in, size_t in_len, char *out, size_t out_sz) {
    /* Base64 URL-safe without padding. For 32 bytes input we need <= 44 chars. */
    char tmp[64];
    size_t n = base64_encode(in, in_len, tmp);
    tmp[n] = '\0';
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        char c = tmp[i];
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
        else if (c == '=') continue; /* strip padding */
        if (o + 1 >= out_sz) return -1;
        out[o++] = c;
    }
    if (o >= out_sz) return -1;
    out[o] = '\0';
    return 0;
}

int validate_username(const char *username) {
    if (!username) return -1;
    size_t n = strlen(username);
    if (n < 3 || n > 32) return -1;
    for (size_t i = 0; i < n; i++) {
        char c = username[i];
        if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return -1;
    }
    return 0;
}

void lowercase_ascii(char *s) {
    if (!s) return;
    for (; *s; ++s) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
}

static int random_bytes(unsigned char *buf, size_t len) {
#if defined(__APPLE__)
    if (CCRandomGenerateBytes(buf, len) != kCCSuccess) return -1;
    return 0;
#else
    if (RAND_bytes(buf, (int)len) != 1) return -1;
    return 0;
#endif
}

int generate_session_id(char *out, size_t out_sz) {
    unsigned char rnd[32];
    if (random_bytes(rnd, sizeof(rnd)) < 0) return -1;
    return b64url_from_bytes(rnd, sizeof(rnd), out, out_sz);
}

int hash_password_pbkdf2(const char *password, char *out, size_t out_sz) {
    const unsigned iter = 200000;
    unsigned char salt[16];
    if (random_bytes(salt, sizeof(salt)) < 0) return -1;
    unsigned char dk[32];
#if defined(__APPLE__)
    int ok = CCKeyDerivationPBKDF(kCCPBKDF2, password, (unsigned)strlen(password),
        salt, sizeof(salt), kCCPRFHmacAlgSHA256, iter, dk, sizeof(dk));
    if (ok != kCCSuccess) return -1;
#else
    if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, (int)sizeof(salt),
            (int)iter, EVP_sha256(), (int)sizeof(dk), dk) != 1) return -1;
#endif
    char salt_b64[64], dk_b64[64];
    size_t s_len = base64_encode(salt, sizeof(salt), salt_b64);
    size_t d_len = base64_encode(dk, sizeof(dk), dk_b64);
    if (s_len == 0 || d_len == 0) return -1;
    salt_b64[s_len] = '\0';
    dk_b64[d_len] = '\0';
    snprintf(out, out_sz, "pbkdf2$sha256$iter=%u$%s$%s", iter, salt_b64, dk_b64);
    return 0;
}

static int constant_time_eq(const unsigned char *a, const unsigned char *b, size_t n) {
    unsigned char v = 0;
    for (size_t i = 0; i < n; i++) v |= a[i] ^ b[i];
    return v == 0;
}

/* Simple base64 decoder for password verification */
static int base64_decode_simple(const char *in, unsigned char *out, size_t outcap, size_t *outlen) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    static int inv_table[256];
    static int table_initialized = 0;
    
    if (!table_initialized) {
        for (int i = 0; i < 256; i++) inv_table[i] = -1;
        for (int i = 0; i < 64; i++) inv_table[(unsigned char)table[i]] = i;
        table_initialized = 1;
    }
    
    size_t len = strlen(in);
    size_t o = 0;
    
    for (size_t i = 0; i < len && o < outcap; i += 4) {
        if (i + 3 >= len) break;
        
        int c1 = inv_table[(unsigned char)in[i]];
        int c2 = inv_table[(unsigned char)in[i+1]];
        int c3 = inv_table[(unsigned char)in[i+2]];
        int c4 = inv_table[(unsigned char)in[i+3]];
        
        if (c1 < 0 || c2 < 0) break;
        
        if (o < outcap) out[o++] = (unsigned char)((c1 << 2) | (c2 >> 4));
        if (c3 >= 0 && o < outcap) out[o++] = (unsigned char)(((c2 & 0xF) << 4) | (c3 >> 2));
        if (c4 >= 0 && o < outcap) out[o++] = (unsigned char)(((c3 & 0x3) << 6) | c4);
    }
    
    *outlen = o;
    return 0;
}

int verify_password_pbkdf2(const char *password, const char *stored) {
    /* Parse: pbkdf2$sha256$iter=NNN$salt_b64$dk_b64 */
    /* Note: prefix length is 19, not 20 */
    if (strncmp(stored, "pbkdf2$sha256$iter=", 19) != 0) return -1;
    const char *p = stored + 19;
    char *end = NULL;
    long iter = strtol(p, &end, 10);
    if (iter <= 0 || !end || *end != '$') return -1;
    end++;
    const char *salt_b64 = end;
    const char *sep = strchr(salt_b64, '$');
    if (!sep) return -1;
    const char *dk_b64 = sep + 1;

    /* Copy base64 substrings into temporary, null-terminated buffers */
    size_t salt_len = (size_t)(sep - salt_b64);
    if (salt_len == 0 || salt_len >= 64) return -1;
    char salt_copy[64];
    memcpy(salt_copy, salt_b64, salt_len);
    salt_copy[salt_len] = '\0';

    size_t dk_len = strlen(dk_b64);
    if (dk_len == 0 || dk_len >= 64) return -1;
    char dk_copy[64];
    memcpy(dk_copy, dk_b64, dk_len);
    dk_copy[dk_len] = '\0';

    /* Decode salt and stored dk */
    unsigned char salt[16], stored_dk[32];
    size_t salt_decoded_len, dk_decoded_len;
    
    if (base64_decode_simple(salt_copy, salt, sizeof(salt), &salt_decoded_len) < 0) return -1;
    if (salt_decoded_len != 16) return -1;
    
    if (base64_decode_simple(dk_copy, stored_dk, sizeof(stored_dk), &dk_decoded_len) < 0) return -1;
    if (dk_decoded_len != 32) return -1;

    /* Recompute PBKDF2 with same parameters */
    unsigned char computed_dk[32];
#if defined(__APPLE__)
    int ok = CCKeyDerivationPBKDF(kCCPBKDF2, password, (unsigned)strlen(password),
        salt, sizeof(salt), kCCPRFHmacAlgSHA256, (unsigned)iter, computed_dk, sizeof(computed_dk));
    if (ok != kCCSuccess) return -1;
#else
    if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, (int)sizeof(salt),
            (int)iter, EVP_sha256(), (int)sizeof(computed_dk), computed_dk) != 1) return -1;
#endif

    /* Constant-time comparison */
    int ok_cmp = constant_time_eq(computed_dk, stored_dk, 32) ? 1 : 0;
    if (!ok_cmp) {
        printf("[verify] iter=%ld salt_len=%zu dk_len=%zu decoded_salt=%zu decoded_dk=%zu\n",
            iter, salt_len, dk_len, salt_decoded_len, dk_decoded_len);
        printf("[verify] stored_dk[0..3]=%02x%02x%02x%02x computed[0..3]=%02x%02x%02x%02x\n",
            stored_dk[0], stored_dk[1], stored_dk[2], stored_dk[3],
            computed_dk[0], computed_dk[1], computed_dk[2], computed_dk[3]);
        fflush(stdout);
    }
    return ok_cmp;
}

int get_cookie_value(const char *headers, const char *name, char *out, size_t out_sz) {
    if (!headers || !name) return 0;
    const char *h = headers;
    while ((h = strcasestr(h, "\r\nCookie:")) != NULL) {
        h += 9;
        while (*h == ' ' || *h == '\t') h++;
        const char *eol = strstr(h, "\r\n");
        if (!eol) eol = h + strlen(h);
        const char *p = h;
        while (p < eol) {
            while (p < eol && (*p == ' ' || *p == '\t' || *p == ';')) p++;
            const char *eq = strchr(p, '=');
            if (!eq || eq >= eol) break;
            size_t klen = (size_t)(eq - p);
            if (klen == strlen(name) && strncasecmp(p, name, klen) == 0) {
                const char *v = eq + 1;
                const char *vend = v;
                while (vend < eol && *vend != ';') vend++;
                size_t len = (size_t)(vend - v);
                if (len + 1 > out_sz) len = out_sz - 1;
                memcpy(out, v, len);
                out[len] = '\0';
                return 1;
            }
            const char *semi = strchr(eq, ';');
            if (!semi || semi >= eol) break;
            p = semi + 1;
        }
    }
    return 0;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static char from_hex_pair(char a, char b) {
    int va = hexval(a), vb = hexval(b);
    if (va < 0 || vb < 0) return 0;
    return (char)((va << 4) | vb);
}

int form_get_kv(const char *body, const char *key, char *out, size_t out_sz) {
    /* very small x-www-form-urlencoded parser for key=value&...; supports %XX and + */
    if (!body || !key || !out || out_sz == 0) return 0;
    size_t klen = strlen(key);
    const char *p = body;
    while (*p) {
        const char *eq = strchr(p, '=');
        if (!eq) break;
        if ((size_t)(eq - p) == klen && strncmp(p, key, klen) == 0) {
            const char *v = eq + 1;
            const char *amp = strchr(v, '&');
            size_t vlen = amp ? (size_t)(amp - v) : strlen(v);
            size_t o = 0;
            for (size_t i = 0; i < vlen && o + 1 < out_sz; i++) {
                char c = v[i];
                if (c == '+') c = ' ';
                else if (c == '%' && i + 2 < vlen) { c = from_hex_pair(v[i+1], v[i+2]); i += 2; }
                out[o++] = c;
            }
            out[o] = '\0';
            return 1;
        }
        const char *amp = strchr(eq + 1, '&');
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}