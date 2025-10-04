#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

// Base64 encoding function
size_t base64_encode(const unsigned char *input, size_t input_len, char *out);

#endif // BASE64_H

