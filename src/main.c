#define _DARWIN_C_SOURCE
#include <arpa/inet.h>
#include <errno.h>
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

#include "http.h"
#include "websocket.h"
#include "util.h"

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

