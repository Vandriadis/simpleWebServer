#ifndef HTTP_H
#define HTTP_H

// HTTP response constants
extern const char *INDEX_HTML;
extern const char *BAD_REQUEST;
extern const char *NOT_FOUND;

// HTTP request parsing
int parse_http_request(char *req, char **method, char **path, char **ws_key);

#endif // HTTP_H

