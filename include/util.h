#ifndef UTIL_H
#define UTIL_H

#include <fcntl.h>
#include <signal.h>

// Nonblocking helper
int set_nonblock(int fd);

// Signal handling
extern volatile sig_atomic_t g_stop;
void on_sigint(int sig);

#endif // UTIL_H

