#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stddef.h>
#include <stdint.h>

// WebSocket handshake
void compute_ws_accept(const char *client_key, char *accept_out);

// WebSocket frame handling
int ws_send_text(int fd, const char *msg, size_t len);
int ws_read_and_echo(int fd);

int ws_read_text(int fd, unsigned char **out, size_t *len_out);

#endif // WEBSOCKET_H

