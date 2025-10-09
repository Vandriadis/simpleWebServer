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
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#include "http.h"
#include "websocket.h"
#include "util.h"
#include "db.h"
#include "auth.h"

typedef enum { CONN_HTTP=0, CONN_WS=1 } ConnType;
typedef struct {
	int fd;
	ConnType type;
	int user_id;              /* for WS */
	char username[33];        /* for WS */
} Conn;

static int read_remaining(int fd, char *buf, int already_have, int total_need) {
    int off = already_have;
    while (off < total_need) {
        fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
        struct timeval tv; tv.tv_sec = 2; tv.tv_usec = 0; /* small timeout */
        int rdy = select(fd+1, &set, NULL, NULL, &tv);
        if (rdy <= 0) return -1; /* timeout or error */
        ssize_t got = recv(fd, buf + off, (size_t)(total_need - off), 0);
        if (got <= 0) return -1;
        off += (int)got;
    }
    return 0;
}

static void send_simple(int fd, const char *status, const char *ctype, const char *body) {
	char hdr[512];
	int blen = (int)strlen(body);
	int n = snprintf(hdr, sizeof(hdr),
		"HTTP/1.1 %s\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %d\r\n"
		"Connection: close\r\n"
		"\r\n", status, ctype, blen);
	send(fd, hdr, (size_t)n, 0);
	if (blen) send(fd, body, (size_t)blen, 0);
}

static void send_json(int fd, const char *status, const char *json) {
	send_simple(fd, status, "application/json; charset=utf-8", json ? json : "");
}

static void set_cookie_and_no_content(int fd, const char *name, const char *value, int max_age) {
	char hdr[512];
	int n = snprintf(hdr, sizeof(hdr),
		"HTTP/1.1 204 No Content\r\n"
		"Set-Cookie: %s=%s; HttpOnly; SameSite=Lax; Path=/; Max-Age=%d\r\n"
		"Connection: close\r\n"
		"Content-Length: 0\r\n\r\n", name, value, max_age);
	send(fd, hdr, (size_t)n, 0);
}

// helper for building JSON message array
struct msg_builder {
	char *buf;
	int offset;
	int first;
};

static void append_message_json(const char *username, const char *content, long ts, void *userdata) {
	struct msg_builder *mb = (struct msg_builder*)userdata;
	if (!mb->first) mb->offset += sprintf(mb->buf + mb->offset, ",");
	mb->first = 0;
	
	// basic JSON escaping for quotes and backslashes
	char esc[4096] = {0};
	const char *p = content;
	char *out = esc;
	while (*p && (out - esc) < 4000) {
		if (*p == '"' || *p == '\\') *out++ = '\\';
		*out++ = *p++;
	}
	*out = '\0';
	
	mb->offset += sprintf(mb->buf + mb->offset,
		"{\"username\":\"%s\",\"content\":\"%s\",\"timestamp\":%ld}",
		username, esc, ts);
}

// get MIME type based on file extension
static const char* get_mime_type(const char *path) {
	const char *ext = strrchr(path, '.');
	if (!ext) return "application/octet-stream";
	if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
	if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
	if (strcmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
	if (strcmp(ext, ".json") == 0) return "application/json; charset=utf-8";
	if (strcmp(ext, ".png") == 0) return "image/png";
	if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
	if (strcmp(ext, ".gif") == 0) return "image/gif";
	if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
	if (strcmp(ext, ".ico") == 0) return "image/x-icon";
	return "application/octet-stream";
}

// serve a static file
static void serve_file(int fd, const char *filepath) {
	FILE *f = fopen(filepath, "rb");
	if (!f) {
		send(fd, NOT_FOUND, strlen(NOT_FOUND), 0);
		return;
	}
	
	// get file size
	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	
	if (fsize < 0 || fsize > 10*1024*1024) {  // max 10MB
		fclose(f);
		send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
		return;
	}
	
	// send headers
	const char *mime = get_mime_type(filepath);
	char hdr[512];
	int n = snprintf(hdr, sizeof(hdr),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: %s\r\n"
		"Content-Length: %ld\r\n"
		"Connection: close\r\n"
		"\r\n", mime, fsize);
	send(fd, hdr, (size_t)n, 0);
	
	// send file content
	char buf[8192];
	size_t r;
	while ((r = fread(buf, 1, sizeof(buf), f)) > 0) {
		send(fd, buf, r, 0);
	}
	fclose(f);
}

int main(void) {
	signal(SIGINT, on_sigint);
	signal(SIGPIPE, SIG_IGN);

	if (db_init("db.sqlite3") < 0) {
		fprintf(stderr, "db init failed\n");
		return 1;
	}

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
	addr.sin_port = htons(8081);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
	if (listen(srv, 16) < 0) { perror("listen"); return 1; }
	set_nonblock(srv);

	printf("Listening on http://127.0.0.1:8081  (Ctrl+C to stop)\n");

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
					conns[i].fd = cfd; conns[i].type = CONN_HTTP; conns[i].user_id = 0; conns[i].username[0] = '\0'; placed = 1; break;
				}
				if (!placed) close(cfd);
			}
		}

		for (int i = 0; i < FD_SETSIZE; i++) if (conns[i].fd >= 0 && FD_ISSET(conns[i].fd, &rfds)) {
			int fd = conns[i].fd;
			if (conns[i].type == CONN_WS) {
				unsigned char *msg = NULL; size_t mlen = 0;
				int r = ws_read_text(fd, &msg, &mlen);
				if (r < 0) { close(fd); conns[i].fd = -1; continue; }
				if (r == 1) {
					// save message to db
					const char *username = conns[i].username[0] ? conns[i].username : "anon";
					db_save_message(conns[i].user_id, username, (const char*)msg);
					
					// broadcast to all WS conns with username prefix
					char prefix[64];
					int pn = snprintf(prefix, sizeof(prefix), "[%s] ", username);
					for (int k = 0; k < FD_SETSIZE; k++) if (conns[k].fd >= 0 && conns[k].type == CONN_WS) {
						ws_send_text(conns[k].fd, prefix, (size_t)pn);
						ws_send_text(conns[k].fd, (const char*)msg, mlen);
					}
					free(msg);
				}
				continue;
			}

			/* HTTP request */
			char buf[8192];
			ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
			if (n <= 0) { close(fd); conns[i].fd = -1; continue; }
			buf[n] = '\0';

			if (!strstr(buf, "\r\n\r\n")) {
				send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
				close(fd); conns[i].fd = -1; continue;
			}

            /* IMPORTANT: parse on a temporary copy so original headers
               remain intact for later body parsing (strtok mutates input) */
            char header_copy[8192];
            memcpy(header_copy, buf, sizeof(header_copy));
            header_copy[sizeof(header_copy)-1] = '\0';
            char *method=NULL, *path=NULL, *ws_key=NULL;
            if (parse_http_request(header_copy, &method, &path, &ws_key) < 0) {
				send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
				close(fd); conns[i].fd = -1; continue;
			}

			// serve index.html for root
			if (strcasecmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
				serve_file(fd, "static/index.html");
				close(fd); conns[i].fd = -1; continue;
			}
			
			// serve static files
			if (strcasecmp(method, "GET") == 0 && strncmp(path, "/static/", 8) == 0) {
				// security: prevent directory traversal
				if (strstr(path, "..")) {
					send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
					close(fd); conns[i].fd = -1; continue;
				}
				// remove leading slash: /static/app.js -> static/app.js
				serve_file(fd, path + 1);
				close(fd); conns[i].fd = -1; continue;
			}

			/* GET /me -> returns {"username":"..."} if session valid */
            if (strcasecmp(method, "GET") == 0 && strcmp(path, "/me") == 0) {
				char cookie[1024];
				int have = get_header_value(buf, "Cookie", cookie, sizeof(cookie));
				char sid[256] = {0};
				if (have) {
					get_cookie_value(buf, "sid", sid, sizeof(sid));
				}
				int uid = 0;
				int ok = sid[0] ? db_get_session_user(sid, &uid) : 0;
				if (ok == 1) {
                    char uname[64];
                    if (db_get_username_by_id(uid, uname, sizeof(uname)) == 0) {
                        char body[128];
                        snprintf(body, sizeof(body), "{\"username\":\"%s\"}", uname);
                        send_json(fd, "200 OK", body);
                    } else {
                        send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
                    }
				} else {
					send(fd, UNAUTHORIZED, strlen(UNAUTHORIZED), 0);
				}
				close(fd); conns[i].fd = -1; continue;
			}

			/* GET /messages -> get chat history (auth required) */
			if (strcasecmp(method, "GET") == 0 && strcmp(path, "/messages") == 0) {
				char sid[256] = {0};
				get_cookie_value(buf, "sid", sid, sizeof(sid));
				int uid = 0;
				if (sid[0] == 0 || db_get_session_user(sid, &uid) != 1) {
					send(fd, UNAUTHORIZED, strlen(UNAUTHORIZED), 0);
					close(fd); conns[i].fd = -1; continue;
				}
				// build JSON array of messages
				char *resp = malloc(65536); // plenty of space
				if (!resp) {
					send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
					close(fd); conns[i].fd = -1; continue;
				}
				
				struct msg_builder mb = { resp, 0, 1 };
				mb.offset = sprintf(resp, "[");
				db_get_messages(100, append_message_json, &mb);
				mb.offset += sprintf(resp + mb.offset, "]");
				
				char hdr[256];
				int n = snprintf(hdr, sizeof(hdr),
					"HTTP/1.1 200 OK\r\n"
					"Content-Type: application/json; charset=utf-8\r\n"
					"Content-Length: %d\r\n"
					"Connection: close\r\n\r\n", mb.offset);
				send(fd, hdr, (size_t)n, 0);
				send(fd, resp, (size_t)mb.offset, 0);
				free(resp);
				close(fd); conns[i].fd = -1; continue;
			}

            /* POST /register (x-www-form-urlencoded: username=...&password=...) */
			if (strcasecmp(method, "POST") == 0 && strcmp(path, "/register") == 0) {
				int clen = get_content_length(buf);
                if (clen < 0 || clen > 1<<20) { send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue; }
                char *hdr_end = strstr(buf, "\r\n\r\n");
                hdr_end = hdr_end ? hdr_end + 4 : NULL;
                if (!hdr_end) { send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue; }
                int have = (int)(n - (hdr_end - buf));
                char *body = hdr_end;
                char *dyn = NULL;
                if (have < clen) {
                    dyn = (char*)malloc((size_t)clen + 1);
                    if (!dyn) { send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue; }
                    if (have > 0) memcpy(dyn, body, (size_t)have);
                    if (read_remaining(fd, dyn, have, clen) < 0) { free(dyn); send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue; }
                    dyn[clen] = '\0';
                    body = dyn;
                } else {
                    body[clen] = '\0';
                }
				char username[64]={0}, password[256]={0};
				if (!form_get_kv(body, "username", username, sizeof(username)) ||
				    !form_get_kv(body, "password", password, sizeof(password))) {
                    if (dyn) free(dyn); send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue;
				}
                if (dyn) free(dyn);
				lowercase_ascii(username);
				if (validate_username(username) < 0 || strlen(password) < 8) {
					send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue;
				}
				char ph[256];
				if (hash_password_pbkdf2(password, ph, sizeof(ph)) < 0) {
					send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue;
				}
				int r = db_create_user(username, ph);
				if (r == -2) {
					send_json(fd, "409 Conflict", "{\"error\":\"username_taken\"}");
				} else if (r == 0) {
					send_simple(fd, "201 Created", "text/plain; charset=utf-8", "ok");
				} else {
					send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
				}
				close(fd); conns[i].fd = -1; continue;
			}

            /* POST /login */
			if (strcasecmp(method, "POST") == 0 && strcmp(path, "/login") == 0) {
				int clen = get_content_length(buf);
                if (clen < 0 || clen > 1<<20) { send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue; }
                char *hdr_end = strstr(buf, "\r\n\r\n");
                hdr_end = hdr_end ? hdr_end + 4 : NULL;
                if (!hdr_end) { send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue; }
                int have = (int)(n - (hdr_end - buf));
                char *body = hdr_end;
                char *dyn = NULL;
                if (have < clen) {
                    dyn = (char*)malloc((size_t)clen + 1);
                    if (!dyn) { send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue; }
                    if (have > 0) memcpy(dyn, body, (size_t)have);
                    if (read_remaining(fd, dyn, have, clen) < 0) { free(dyn); send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue; }
                    dyn[clen] = '\0';
                    body = dyn;
                } else {
                    body[clen] = '\0';
                }
				char username[64]={0}, password[256]={0};
				if (!form_get_kv(body, "username", username, sizeof(username)) ||
				    !form_get_kv(body, "password", password, sizeof(password))) {
                    if (dyn) free(dyn); send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0); close(fd); conns[i].fd=-1; continue;
				}
                if (dyn) free(dyn);
				lowercase_ascii(username);
				int uid = 0;
				char stored[256];
				if (db_get_user_by_username(username, &uid, stored, sizeof(stored)) < 0) {
					fprintf(stderr, "[login] user not found: %s\n", username);
					send(fd, UNAUTHORIZED, strlen(UNAUTHORIZED), 0);
					close(fd); conns[i].fd=-1; continue;
				}
				/* IMPORTANT: implement verify_password_pbkdf2() fully; currently stub returns -1 */
				if (verify_password_pbkdf2(password, stored) != 1) {
					fprintf(stderr, "[login] bad password for: %s\n", username);
					send(fd, UNAUTHORIZED, strlen(UNAUTHORIZED), 0);
					close(fd); conns[i].fd=-1; continue;
				}
				char sid[128];
				if (generate_session_id(sid, sizeof(sid)) < 0) {
					send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
					close(fd); conns[i].fd=-1; continue;
				}
				long ttl = 7*24*3600;
				if (db_create_session(sid, uid, time(NULL)+ttl) < 0) {
					send(fd, BAD_REQUEST, strlen(BAD_REQUEST), 0);
					close(fd); conns[i].fd=-1; continue;
				}
				set_cookie_and_no_content(fd, "sid", sid, (int)ttl);
				close(fd); conns[i].fd=-1; continue;
			}

			/* POST /logout */
			if (strcasecmp(method, "POST") == 0 && strcmp(path, "/logout") == 0) {
				char sid[256]={0};
				get_cookie_value(buf, "sid", sid, sizeof(sid));
				if (sid[0]) db_delete_session(sid);
				set_cookie_and_no_content(fd, "sid", "deleted", 0);
				close(fd); conns[i].fd=-1; continue;
			}

			/* WS upgrade with auth via Cookie sid */
            if (strcmp(path, "/ws") == 0 && ws_key) {
				char sid[256]={0};
				get_cookie_value(buf, "sid", sid, sizeof(sid));
				int uid = 0;
				int ok = sid[0] ? db_get_session_user(sid, &uid) : 0;
				if (ok != 1) {
					send(fd, UNAUTHORIZED, strlen(UNAUTHORIZED), 0);
					close(fd); conns[i].fd = -1; continue;
				}
				char accept[64]; compute_ws_accept(ws_key, accept);
				char resp[512];
				int m = snprintf(resp, sizeof(resp),
					"HTTP/1.1 101 Switching Protocols\r\n"
					"Connection: Upgrade\r\n"
					"Upgrade: websocket\r\n"
					"Sec-WebSocket-Accept: %s\r\n\r\n", accept);
				send(fd, resp, (size_t)m, 0);
				printf("[upgrade] client fd=%d -> WebSocket (uid=%d)\n", fd, uid);
				fflush(stdout);
				conns[i].type = CONN_WS;
				conns[i].user_id = uid;
                if (db_get_username_by_id(uid, conns[i].username, sizeof(conns[i].username)) != 0) {
                    snprintf(conns[i].username, sizeof(conns[i].username), "user%d", uid);
                }
				continue;
			}

			send(fd, NOT_FOUND, strlen(NOT_FOUND), 0);
			close(fd); conns[i].fd = -1;
		}

	}

	for (int i = 0; i < FD_SETSIZE; i++) if (conns[i].fd >= 0) close(conns[i].fd);
	close(srv);
	db_close();
	printf("Server stopped\n");
	return 0;
}