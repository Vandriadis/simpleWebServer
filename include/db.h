#ifndef DB_H
#define DB_H

#include <stddef.h>

int db_init(const char *db_path);
void db_close(void);
int db_create_user(const char *username, const char *password_hash);
int db_get_user_by_username(const char *username, int*user_id, char *password_hash_out, size_t out_sz);
int db_create_session(const char *sid, int user_id, long expires_at);
int db_get_session_user(const char *sid, int *user_id);
int db_delete_session(const char *sid);
int db_get_username_by_id(int user_id, char *out, size_t out_sz);
#endif