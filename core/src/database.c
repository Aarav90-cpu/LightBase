#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

// 🎯 Forward declaration of the ledger initializer from collections.c
int initialize_system_collections_ledger_table(sqlite3 *db);

// --- ARENA-POWERED DATABASE CONNECTION CONSTRUCTOR SUBROUTINE ---
sqlite3 *open_arena_database_connection(const char *db_filename) {
    sqlite3 *db_handle = NULL;

    printf("[C-Core] Dynamic opening database via Arena Allocations: %s\n", db_filename);
    int status = sqlite3_open(db_filename, &db_handle);

    if (status != SQLITE_OK) {
        fprintf(stderr, "[C-Core Fault] Failed to secure file descriptor for %s\n", db_filename);
        return NULL;
    }

    // 🎯 INITIALIZE THE SYSTEM COLLECTIONS LEDGER AUTOMATICALLY NATIVELY ON THE SPOT!
    int ledger_status = initialize_system_collections_ledger_table(db_handle);
    if (ledger_status != 0) {
        fprintf(stderr, "[C-Core Warning] Ledger table initialization reported issues.\n");
    }

    return db_handle;
}