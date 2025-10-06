#include "db.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static sqlite3 *g_db = NULL;

static int db_exec(const char *sql) {
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    rc = sqlite3_step(stmt);
    int ok = (rc == SQLITE_DONE || rc == SQLITE_ROW) ? 0 : -1;
    sqlite3_finalize(stmt);
    return ok;
}

int db_init(const char *db_path) {
    if (sqlite3_open(db_path, &g_db) != SQLITE_OK) {
        fprintf(stderr, "Failed to open database: %s\n", sqlite3_errmsg(g_db));
        return -1;
    }
    db_exec("PRAGMA foreign_keys = ON");
    db_exec("PRAGMA journal_mode = WAL");
    const char *schema_users =
		"CREATE TABLE IF NOT EXISTS users ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"username TEXT UNIQUE NOT NULL,"
		"password_hash TEXT NOT NULL,"
		"created_at INTEGER NOT NULL"
		");";
	const char *schema_sessions =
		"CREATE TABLE IF NOT EXISTS sessions ("
		"id TEXT PRIMARY KEY,"
		"user_id INTEGER NOT NULL,"
		"created_at INTEGER NOT NULL,"
		"expires_at INTEGER NOT NULL,"
		"user_agent TEXT,"
		"ip TEXT,"
		"FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE"
		");";
	if (db_exec(schema_users) < 0) return -1;
	if (db_exec(schema_sessions) < 0) return -1;
	return 0;
}

void db_close(void) {
    if (g_db) { sqlite3_close(g_db); g_db = NULL; }
}

int db_create_user(const char *username, const char *password_hash) {
    static const char *sql = "INSERT INTO users (username, password_hash, created_at) VALUES (?, ?, ?);";
	sqlite3_stmt *st = NULL;
	if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(st, 1, username, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 2, password_hash, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(st, 3, (sqlite3_int64)time(NULL));
	int rc = sqlite3_step(st);
	int out = 0;
	if (rc != SQLITE_DONE) {
		if (rc == SQLITE_CONSTRAINT) out = -2;
		else out = -1;
	}
	sqlite3_finalize(st);
	return out;
}

int db_get_user_by_username(const char *username, int*user_id, char *password_hash_out, size_t out_sz) {
    static const char *sql = "SELECT id, password_hash FROM users WHERE username = ?;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, username, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) { sqlite3_finalize(st); return -1; }
    *user_id = sqlite3_column_int(st, 0);
    const unsigned char *ph = sqlite3_column_text(st, 1);
    if (!ph) { sqlite3_finalize(st); return -1; }
    snprintf(password_hash_out, out_sz, "%s", (const char*)ph);
    sqlite3_finalize(st);
    return 0;
}

int db_create_session(const char *sid, int user_id, long expires_at) {
    static const char *sql = "INSERT INTO sessions (id, user_id, created_at, expires_at) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, sid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, user_id);
    sqlite3_bind_int64(st, 3, (sqlite3_int64)time(NULL));
    sqlite3_bind_int64(st, 4, (sqlite3_int64)expires_at);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_session_user(const char *sid, int *user_id) {
    static const char *sql = "SELECT user_id, expires_at FROM sessions WHERE id = ?;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
	sqlite3_bind_text(st, 1, sid, -1, SQLITE_TRANSIENT);
	int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) { sqlite3_finalize(st); return 0; }
    int uid = sqlite3_column_int(st, 0);
    long exp = (long)sqlite3_column_int64(st, 1);
	sqlite3_finalize(st);
	if (exp < (long)time(NULL)) {
		db_delete_session(sid);
		return 0;
	}
	*user_id = uid;
	return 1;
}

int db_delete_session(const char *sid) {
    static const char *sql = "DELETE FROM sessions WHERE id = ?;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(st, 1, sid, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

int db_get_username_by_id(int user_id, char *out, size_t out_sz) {
    static const char *sql = "SELECT username FROM users WHERE id = ?;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(g_db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, user_id);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) { sqlite3_finalize(st); return -1; }
    const unsigned char *u = sqlite3_column_text(st, 0);
    if (!u) { sqlite3_finalize(st); return -1; }
    snprintf(out, out_sz, "%s", (const char*)u);
    sqlite3_finalize(st);
    return 0;
}
    