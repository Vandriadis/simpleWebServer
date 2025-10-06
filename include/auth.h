#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>

int validate_username(const char *username);
void lowercase_ascii(char *s);

/* PBKDF2-HMAC-SHA256: returns 0 on success, writes "pbkdf2$sha256$iter=200000$salt_b64$dk_b64" */
int hash_password_pbkdf2(const char *password, char *out, size_t out_sz);

/* returns 1 if match, 0 if no match, -1 on error */
int verify_password_pbkdf2(const char *password, const char *stored);

/* session id: base64url (no padding) of 32 random bytes; returns 0 on success */
int generate_session_id(char *out, size_t out_sz);

/* cookie parsing: returns 1 if found and copied into out, else 0 */
int get_cookie_value(const char *headers, const char *name, char *out, size_t out_sz);
int form_get_kv(const char *body, const char *key, char *out, size_t out_sz);

#endif