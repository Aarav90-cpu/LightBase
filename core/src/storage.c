#include "storage.h"
#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

EnvironmentBlock* current_runtime_env = NULL;
MemoryArena* global_env_arena = NULL;

// Initialize our fixed binary loop runway cleanly on the filesystem
int init_log_ring_buffer(void) {
    FILE* file = fopen(STORAGE_FILE_PATH, "rb+");
    if (!file) {
        // If file doesn't exist, create it and pre-allocate its exact fixed capacity bounds instantly
        file = fopen(STORAGE_FILE_PATH, "wb+");
        if (!file) return -1;

        TelemetryRecord empty_record = {0, 0.0f, 0, 0, {0}};
        for (int i = 0; i < MAX_STORAGE_RECORDS; i++) {
            fwrite(&empty_record, sizeof(TelemetryRecord), 1, file);
        }
    }
    fclose(file);
    return 0;
}

// Write the next record slot sequentially, wrapping around using basic byte algebra
int append_telemetry_record(float cpu, uint32_t total_mem, uint32_t avail_mem) {
    FILE* file = fopen(STORAGE_FILE_PATH, "rb+");
    if (!file) return -1;

    TelemetryRecord current;
    int next_slot_index = 0;
    uint64_t oldest_timestamp = UINT64_MAX; // Fixed 64-bit bounds initialization
    uint64_t youngest_timestamp = 0;
    int oldest_slot = 0;
    int youngest_slot = 0;

    for (int i = 0; i < MAX_STORAGE_RECORDS; i++) {
        fseek(file, i * sizeof(TelemetryRecord), SEEK_SET);
        if (fread(&current, sizeof(TelemetryRecord), 1, file) != 1) continue;

        if (current.timestamp == 0) {
            next_slot_index = i;
            break;
        }
        if (current.timestamp < oldest_timestamp) {
            oldest_timestamp = current.timestamp;
            oldest_slot = i;
        }
        if (current.timestamp > youngest_timestamp) {
            youngest_timestamp = current.timestamp;
            youngest_slot = i;
        }

        // Default fallback ring loop logic boundary pointer configuration
        if (i == MAX_STORAGE_RECORDS - 1) {
            next_slot_index = (youngest_slot + 1) % MAX_STORAGE_RECORDS;
        }
    }

    // Populate our packed binary structure frame parameters
    TelemetryRecord record;
    record.timestamp = (uint64_t)time(NULL);
    record.cpu_usage = cpu;
    record.mem_total_mb = total_mem;
    record.mem_avail_mb = avail_mem;
    memset(record.reserved, 0, sizeof(record.reserved));

    // Fast-jump directly to the target byte offset position slice on disk and slap down the record
    fseek(file, next_slot_index * sizeof(TelemetryRecord), SEEK_SET);
    fwrite(&record, sizeof(TelemetryRecord), 1, file);

    fclose(file);
    return next_slot_index;
}

// Stream the raw stored records straight out back into memory arrays
int read_all_telemetry_records(TelemetryRecord* out_buffer, int max_records) {
    FILE* file = fopen(STORAGE_FILE_PATH, "rb");
    if (!file) return -1;

    fseek(file, 0, SEEK_SET);
    int records_read = fread(out_buffer, sizeof(TelemetryRecord), max_records, file);

    fclose(file);
    return records_read;
}

// ============================================================================
// 🗄️ BARE-METAL DATABASE SCHEMA METADATA SCANNER CORE
// ============================================================================
EXPORT Response scan_database_schema(const char* db_path) {
    printf("[C-Core Storage] Scanning schema definitions for database: %s\n", db_path);
    Response res = {0, NULL};

    // 1. Dynamically wire up our host's SQLite3 library hooks
    void* sqlite_handle = dlopen("libsqlite3.so.0", RTLD_LAZY);
    if (!sqlite_handle) sqlite_handle = dlopen("libsqlite3.so", RTLD_LAZY);

    if (!sqlite_handle) {
        res.status_code = 500;
        res.payload = "{\"error\": \"SQLite3 library target binaries missing from host OS.\"}";
        return res;
    }

    int (*sqlite3_open)(const char*, void**) = dlsym(sqlite_handle, "sqlite3_open");
    int (*sqlite3_prepare_v2)(void*, const char*, int, void**, const char**) = dlsym(sqlite_handle, "sqlite3_prepare_v2");
    int (*sqlite3_step)(void*) = dlsym(sqlite_handle, "sqlite3_step");
    const unsigned char* (*sqlite3_column_text)(void*, int) = dlsym(sqlite_handle, "sqlite3_column_text");
    int (*sqlite3_finalize)(void*) = dlsym(sqlite_handle, "sqlite3_finalize");
    int (*sqlite3_close)(void*) = dlsym(sqlite_handle, "sqlite3_close");

    if (!sqlite3_open || !sqlite3_prepare_v2 || !sqlite3_step || !sqlite3_column_text || !sqlite3_finalize || !sqlite3_close) {
        dlclose(sqlite_handle);
        res.status_code = 500;
        res.payload = "{\"error\": \"Failed to bind dynamic SQLite3 runtime symbol linkages.\"}";
        return res;
    }

    void* db = NULL;
    if (sqlite3_open(db_path, &db) != 0) {
        dlclose(sqlite_handle);
        res.status_code = 500;
        res.payload = "{\"error\": \"Could not acquire system handle lock on target database.\"}";
        return res;
    }

    // 2. Carve out our 2MB arena pool block to accumulate schema string tokens
    size_t scan_arena_size = 2 * 1024 * 1024;
    MemoryArena* scan_arena = arena_init(scan_arena_size);
    if (!scan_arena) {
        sqlite3_close(db);
        dlclose(sqlite_handle);
        res.status_code = 500;
        res.payload = "{\"error\": \"Out of memory panic during schema scanner arena instantiation.\"}";
        return res;
    }

    // Allocate three distinct json strings from our clean arena track
    char* tables_json = (char*)arena_alloc(scan_arena, 4096);
    char* views_json = (char*)arena_alloc(scan_arena, 4096);
    strcpy(tables_json, "[");
    strcpy(views_json, "[");

    int table_count = 0, view_count = 0;
    void* stmt = NULL;

    // Target the sqlite_master system catalog directly to scan structures
    const char* schema_query = "SELECT type, name FROM sqlite_master WHERE type IN ('table', 'view') AND name NOT LIKE 'sqlite_%';";

    if (sqlite3_prepare_v2(db, schema_query, -1, &stmt, NULL) == 0) {
        while (sqlite3_step(stmt) == 100) { // SQLITE_ROW = 100
            const unsigned char* type_txt = sqlite3_column_text(stmt, 0);
            const unsigned char* name_txt = sqlite3_column_text(stmt, 1);

            if (!type_txt || !name_txt) continue;

            char record_item[256];
            snprintf(record_item, sizeof(record_item), "\"%s\",", (const char*)name_txt);

            if (strcmp((const char*)type_txt, "table") == 0) {
                strcat(tables_json, record_item);
                table_count++;
            } else if (strcmp((const char*)type_txt, "view") == 0) {
                strcat(views_json, record_item);
                view_count++;
            }
        }
        sqlite3_finalize(stmt);
    }

    // Clean up trailing commas neatly
    if (table_count > 0 && tables_json[strlen(tables_json) - 1] == ',') tables_json[strlen(tables_json) - 1] = '\0';
    if (view_count > 0 && views_json[strlen(views_json) - 1] == ',') views_json[strlen(views_json) - 1] = '\0';
    strcat(tables_json, "]");
    strcat(views_json, "]");

    // 3. Assemble our final compound structural JSON tree wrapper object onto the heap
    char* dynamic_schema_payload = malloc(16384);
    if (dynamic_schema_payload) {
        snprintf(dynamic_schema_payload, 16384,
                 "{"
                 "\"tables\": %s,"
                 "\"views\": %s"
                 "}",
                 tables_json, views_json);
        res.status_code = 200;
        res.payload = dynamic_schema_payload;
    } else {
        res.status_code = 500;
        res.payload = "{\"error\": \"Heap allocation fault for final schema JSON payload.\"}";
    }

    // Clean up all resources cleanly
    arena_destroy(scan_arena);
    sqlite3_close(db);
    dlclose(sqlite_handle);

    return res;
}

// ============================================================================
// 🌐 STUDIO MODULES: BARE-METAL ENVIRONMENT ALLOCATOR & POINTER SWAPPER
// ============================================================================
EXPORT Response load_studio_environment_state(const char* env_name, const char* db_path, uint32_t timeout, uint8_t sec_level) {
    printf("[C-Core Environment] Initializing environment mutation swap path -> Target: %s\n", env_name);
    Response res = {0, NULL};

    // Instantiate our static global tracking arena once if it doesn't exist
    if (!global_env_arena) {
        global_env_arena = arena_init(128 * 1024); // Allocate 128KB strictly for environment maps
        if (!global_env_arena) {
            res.status_code = 500;
            res.payload = "{\"error\": \"Failed to allocate system environment mapping arena.\"}";
            return res;
        }
    }

    // Allocate a pristine configuration chunk track inside our dedicated arena block boundaries
    EnvironmentBlock* new_config = (EnvironmentBlock*)arena_alloc(global_env_arena, sizeof(EnvironmentBlock));
    if (!new_config) {
        res.status_code = 500;
        res.payload = "{\"error\": \"Environment manager arena capacity exhaustion fault.\"}";
        return res;
    }

    // Populate the parameters safely inside the arena bounds
    strncpy(new_config->env_name, env_name, sizeof(new_config->env_name) - 1);
    strncpy(new_config->active_db_path, db_path, sizeof(new_config->active_db_path) - 1);
    new_config->socket_timeout_ms = timeout;
    new_config->security_level = sec_level;

    // ⚡ THE INSTANT ATOMIC SWAP PASS
    // We completely overwrite the global pointer register with our newly allocated structure address
    current_runtime_env = new_config;

    printf("[C-Core Environment] Swap complete. Active Target Database mapped onto: %s\n", current_runtime_env->active_db_path);

    char* dynamic_env_response = malloc(512);
    snprintf(dynamic_env_response, 512,
             "{"
             "\"env_status\": \"MUTATED\","
             "\"active_environment\": \"%s\","
             "\"active_db\": \"%s\""
             "}",
             current_runtime_env->env_name, current_runtime_env->active_db_path);

    res.status_code = 200;
    res.payload = dynamic_env_response;
    return res;
}

// ============================================================================
// 🌐 STUDIO MODULES: BARE-METAL HTTP REQUEST BUILDER ENGINE
// ============================================================================
EXPORT Response execute_studio_api_request(const char* verb, const char* host, const char* path, const char* headers, const char* body) {
    printf("[C-Core API Studio] Assembling raw HTTP wire packet -> %s %s%s\n", verb, host, path);
    Response res = {0, NULL};

    // Allocate stack space for the fully formatted request packet
    char raw_http_wire[BUFFER_SIZE] = {0};

    // Assemble the start line
    snprintf(raw_http_wire, sizeof(raw_http_wire), "%s %s HTTP/1.1\r\nHost: %s\r\n", verb, path, host);

    // Append custom headers if the user specified them in the request builder
    if (headers && strlen(headers) > 0) {
        strncat(raw_http_wire, headers, sizeof(raw_http_wire) - strlen(raw_http_wire) - 1);
        strncat(raw_http_wire, "\r\n", sizeof(raw_http_wire) - strlen(raw_http_wire) - 1);
    } else {
        strncat(raw_http_wire, "User-Agent: LightBaseStudio/1.0\r\nConnection: close\r\n", sizeof(raw_http_wire) - strlen(raw_http_wire) - 1);
    }

    // Append the request body if present (for POST/PUT/DELETE methods)
    if (body && strlen(body) > 0) {
        char content_len_header[64];
        snprintf(content_len_header, sizeof(content_len_header), "Content-Length: %lu\r\n", (unsigned long)strlen(body));
        strncat(raw_http_wire, content_len_header, sizeof(raw_http_wire) - strlen(raw_http_wire) - 1);
        strncat(raw_http_wire, "\r\n", sizeof(raw_http_wire) - strlen(raw_http_wire) - 1); // Header/Body boundary line
        strncat(raw_http_wire, body, sizeof(raw_http_wire) - strlen(raw_http_wire) - 1);
    } else {
        strncat(raw_http_wire, "\r\n", sizeof(raw_http_wire) - strlen(raw_http_wire) - 1); // Empty body boundary break
    }

    // For testing and verification, let's wrap our raw assembled wire text block as a payload response
    // In our ultimate production loop, we pass this directly to our OpenSSL socket descriptor!
    char* dynamic_studio_payload = malloc(BUFFER_SIZE);
    snprintf(dynamic_studio_payload, BUFFER_SIZE,
             "{"
             "\"studio_status\": \"ASSEMBLED\","
             "\"raw_wire_bytes\": \"%s\""
             "}",
             raw_http_wire);

    res.status_code = 200;
    res.payload = dynamic_studio_payload;
    return res;
}