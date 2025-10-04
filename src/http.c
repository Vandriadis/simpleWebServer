#include <string.h>
#include "http.h"

// Minimal HTTP responder for "/"
const char *INDEX_HTML =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=utf-8\r\n"
"Connection: close\r\n"
"\r\n"
"<!doctype html><html><head><meta charset='utf-8'><title>WS Echo</title>"
"<style>body{font-family:ui-monospace,monospace;padding:20px}#log{white-space:pre-wrap;border:1px solid #ccc;padding:10px;height:280px;overflow:auto}</style>"
"</head><body>"
"<h3>WebSocket Echo</h3>"
"<div id='log'></div>"
"<input id='msg' placeholder='Type message'/> <button onclick='sendMsg()'>Send</button>"
"<script>"
"const log = s=>{const d=document.getElementById('log');d.textContent+=s+\"\\n\";d.scrollTop=d.scrollHeight};"
"const ws = new WebSocket('ws://'+location.host+'/ws');"
"ws.onopen=()=>log('[open]');"
"ws.onmessage=(e)=>log('[recv] '+e.data);"
"ws.onclose=()=>log('[close]');"
"function sendMsg(){const v=document.getElementById('msg').value; ws.send(v); log('[send] '+v);}"
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

// Small HTTP header parser to get Sec-WebSocket-Key
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

