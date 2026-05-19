#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

// --- NATIVE METADATA COMPILER ENTRY POINT ---
char* compile_native_markdown_schema_spec(const char* db_path) {
    sqlite3 *db;
    sqlite3_stmt *table_stmt, *col_stmt;
    char *markdown_buffer = malloc(65536); // Allocate 64KB document runway frame
    if (!markdown_buffer) return NULL;
    memset(markdown_buffer, 0, 65536);

    // Bootstrap local document header tokens
    strcat(markdown_buffer, "# 📊 LightBase Native Database Schema Specification\n\n");
    strcat(markdown_buffer, "> *Generated natively via LightBase C-Core Documentation Subsystem Engine.*\n\n");
    strcat(markdown_buffer, "---\n\n");

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        snprintf(markdown_buffer, 65536, "💥 [C-Core Error] Failed to secure file descriptor for database target: %s\n", db_path);
        return markdown_buffer;
    }

    // Query the master ledger catalog to find all active table names
    const char *table_query = "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';";
    if (sqlite3_prepare_v2(db, table_query, -1, &table_stmt, NULL) == SQLITE_OK) {

        while (sqlite3_step(table_stmt) == SQLITE_ROW) {
            const char *table_name = (const char*)sqlite3_column_text(table_stmt, 0);

            // Format Table Header Segment
            char table_header[256];
            snprintf(table_header, sizeof(table_header), "## 🗂️ Table: `%s`\n\n", table_name);
            strcat(markdown_buffer, table_header);
            strcat(markdown_buffer, "| Column ID | Field Name | Data Type | Not Null | PK |\n");
            strcat(markdown_buffer, "| :--- | :--- | :--- | :--- | :--- |\n");

            // Introspect table layout attributes using SQLite table_info pragma vector
            char pragma_query[256];
            snprintf(pragma_query, sizeof(pragma_query), "PRAGMA table_info(%s);", table_name);

            if (sqlite3_prepare_v2(db, pragma_query, -1, &col_stmt, NULL) == SQLITE_OK) {
                while (sqlite3_step(col_stmt) == SQLITE_ROW) {
                    int cid = sqlite3_column_int(col_stmt, 0);
                    const char *name = (const char*)sqlite3_column_text(col_stmt, 1);
                    const char *type = (const char*)sqlite3_column_text(col_stmt, 2);
                    int notnull = sqlite3_column_int(col_stmt, 3);
                    int pk = sqlite3_column_int(col_stmt, 4);

                    char row_buffer[512];
                    snprintf(row_buffer, sizeof(row_buffer), "| %d | **%s** | `%s` | %s | %s |\n",
                             cid, name, (type && strlen(type)) ? type : "NONE",
                             notnull ? "✅ YES" : "❌ NO",
                             pk ? "🔑 YES" : "❌ NO");
                    strcat(markdown_buffer, row_buffer);
                }
                sqlite3_finalize(col_stmt);
            }
            strcat(markdown_buffer, "\n---\n\n");
        }
        sqlite3_finalize(table_stmt);
    }

    sqlite3_close(db);
    return markdown_buffer; // Flash raw string block pointer back across ctypes
}