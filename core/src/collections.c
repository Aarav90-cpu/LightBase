#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "engine.h"

// --- AUTO-IGNITION SCHEMA ENFORCER GUARD ---
int initialize_system_collections_ledger_table(sqlite3 *db) {
    char *error_message = NULL;

    // Hardened schema blueprint layout execution string
    const char *sql_schema_blueprint =
        "CREATE TABLE IF NOT EXISTS system_collections ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "method TEXT NOT NULL,"
        "url TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_collections_name ON system_collections(name);";

    printf("[C-Core Storage] Enforcing structural ledger parameters on local database ring...\n");
    int status_code = sqlite3_exec(db, sql_schema_blueprint, NULL, 0, &error_message);

    if (status_code != SQLITE_OK) {
        fprintf(stderr, "[C-Core Bug] Schema generation dropped: %s\n", error_message);
        sqlite3_free(error_message);
        return -1;
    }

    printf("[C-Core Storage] Local Collections Matrix mounted successfully with index caching!\n");
    return 0;
}

// --- ZERO-ALLOCATION BINARY BATCH INSERTER ---
int insert_collection_record_to_arena(sqlite3 *db, const char *name, const char *method, const char *url) {
    sqlite3_stmt *stmt;
    const char *insert_statement = "INSERT INTO system_collections (name, method, url) VALUES (?, ?, ?);";

    // Prepare compiled statement layout byte representation for ultra-fast execution loops
    if (sqlite3_prepare_v2(db, insert_statement, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[C-Core Bug] Failed to compile binary insert query vector\n");
        return -1;
    }

    // Bind memory address pointers cleanly to shield from SQL injection boundary vulnerabilities
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, method, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, url, -1, SQLITE_STATIC);

    int result = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (result != SQLITE_DONE) {
        fprintf(stderr, "[C-Core Bug] Failed to commit collection record down disk lanes\n");
        return -1;
    }

    return 0; // Record locked into storage array successfully!
}

// --- ARENA-POWERED COLLECTIONS LEDGER READER ---
EXPORT char* list_all_collections(const char* db_path) {
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;

    char *json_buffer = malloc(32768);
    if (!json_buffer) return NULL;
    memset(json_buffer, 0, 32768);
    strcpy(json_buffer, "[");

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        snprintf(json_buffer, 32768, "{\"error\": \"Failed to open database for collection listing.\"}");
        return json_buffer;
    }

    initialize_system_collections_ledger_table(db);

    const char *query = "SELECT id, name, method, url FROM system_collections ORDER BY id DESC;";
    int count = 0;

    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt, 0);
            const char *name = (const char*)sqlite3_column_text(stmt, 1);
            const char *method = (const char*)sqlite3_column_text(stmt, 2);
            const char *url = (const char*)sqlite3_column_text(stmt, 3);

            char row_entry[1024];
            snprintf(row_entry, sizeof(row_entry),
                     "%s{\"id\": %d, \"name\": \"%s\", \"method\": \"%s\", \"url\": \"%s\"}",
                     count > 0 ? "," : "",
                     id,
                     name ? name : "",
                     method ? method : "",
                     url ? url : "");

            size_t current_len = strlen(json_buffer);
            size_t entry_len = strlen(row_entry);
            if (current_len + entry_len + 4 < 32768) {
                strcat(json_buffer, row_entry);
            }
            count++;
        }
        sqlite3_finalize(stmt);
    }

    strcat(json_buffer, "]");
    sqlite3_close(db);
    return json_buffer;
}