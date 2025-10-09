#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include "http.h"

// This is kept only for backwards compatibility if someone imports it
// The actual server now serves from static/ directory
const char *INDEX_HTML = NULL;

const char *BAD_REQUEST =
"HTTP/1.1 400 Bad Request\r\n"
"Connection: close\r\n"
"Content-Length: 0\r\n\r\n";

const char *NOT_FOUND =
"HTTP/1.1 404 Not Found\r\n"
"Connection: close\r\n"
"Content-Length: 0\r\n\r\n";

const char *UNAUTHORIZED =
"HTTP/1.1 401 Unauthorized\r\n"
"Connection: close\r\n"
"Content-Length: 0\r\n\r\n";

const char *NO_CONTENT =
"HTTP/1.1 204 No Content\r\n"
"Connection: close\r\n"
"Content-Length: 0\r\n\r\n";

int parse_http_request(char *req, char **method, char **path, char **ws_key) {
	*method = strtok(req, " \t\r\n");
	*path = strtok(NULL, " \t\r\n");
	if (!*method || !*path) return -1;
	*ws_key = NULL;
	char *line = NULL;
	while ((line = strtok(NULL, "\r\n"))) {
		if (strncasecmp(line, "Sec-WebSocket-Key:", 18) == 0) {
			char *p = line + 18;
			while (*p == ' ' || *p == '\t') p++;
			*ws_key = p;
		}
	}
	return 0;
}

int get_header_value(const char *req, const char *name, char *out, int out_sz) {
	size_t nlen = strlen(name);
	const char *p = req;
	while ((p = strcasestr(p, name)) != NULL) {
		if ((p == req || (p > req + 1 && p[-2] == '\r' && p[-1] == '\n')) &&
            strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
			    p += nlen + 1;
			    while (*p == ' ' || *p == '\t') p++;
			    const char *e = strstr(p, "\r\n");
			    if (!e) e = p + strlen(p);
			    int len = (int)(e - p);
			    if (len >= out_sz) len = out_sz - 1;
			    memcpy(out, p, (size_t)len);
			    out[len] = '\0';
			    return 1;
		    }
		p += nlen;
	}
	return 0;
}

int get_content_length(const char *req) {
	char buf[32];
	if (!get_header_value(req, "Content-Length", buf, sizeof(buf))) return -1;
	return atoi(buf);
}