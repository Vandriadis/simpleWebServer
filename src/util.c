#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include "util.h"

// Nonblocking helper
int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Signal handling
volatile sig_atomic_t g_stop = 0;
void on_sigint(int sig) { 
    (void)sig; 
    g_stop = 1; 
}

