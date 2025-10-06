#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include "http.h"

/* Minimal "/" page kept as-is with a tiny login/register UI hint */
const char *INDEX_HTML =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=utf-8\r\n"
"Connection: close\r\n"
"\r\n"
"<!doctype html><html><head><meta charset='utf-8'><title>WS Echo</title>"
"<style>body{font-family:ui-monospace,monospace;padding:20px}#log{white-space:pre-wrap;border:1px solid #ccc;padding:10px;height:280px;overflow:auto}</style>"
"</head><body>"
"<h3>WebSocket Chat</h3>"
"<div>Register/Login (POST forms to /register and /login using x-www-form-urlencoded)</div>"
"<div id='user'></div>"
"<div id='log'></div>"
"<input id='msg' placeholder='Type message'/> <button onclick='sendMsg()'>Send</button>"
"<script>"
"const log = s=>{const d=document.getElementById('log');d.textContent+=s+\"\\n\";d.scrollTop=d.scrollHeight};"
"fetch('/me').then(r=>r.ok?r.json():null).then(j=>{if(j){document.getElementById('user').textContent='Logged in as '+j.username}});"
"const ws = new WebSocket('ws://'+location.host+'/ws');"
"ws.onopen=()=>log('[open]');"
"ws.onmessage=(e)=>log(e.data);"
"ws.onclose=()=>log('[close]');"
"function sendMsg(){const v=document.getElementById('msg').value; ws.send(v);}"
"</script>"
"</body></html>";

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