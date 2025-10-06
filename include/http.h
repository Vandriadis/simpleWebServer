#ifndef HTTP_H
#define HTTP_H

/* existing */
extern const char *INDEX_HTML;
extern const char *BAD_REQUEST;
extern const char *NOT_FOUND;

/* new minimal responses */
extern const char *UNAUTHORIZED;
extern const char *NO_CONTENT;

/* HTTP request parsing */
int parse_http_request(char *req, char **method, char **path, char **ws_key);

/* helpers */
int get_header_value(const char *req, const char *name, char *out, int out_sz);
int get_content_length(const char *req);

#endif