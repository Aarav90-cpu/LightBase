#include "storage.h"
#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sqlite3.h>

#define MMAP_FILE_SIZE (sizeof(TelemetryRecord) * MAX_STORAGE_RECORDS)

extern uint32_t active_log_index;

EnvironmentBlock* current_runtime_env = NULL;
MemoryArena* global_env_arena = NULL;
TelemetryRecord* mmap_ring_buffer = NULL;
int shared_log_fd = -1;

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
// ============================================================================
// 🗄️ SYSTEMS OPTIMIZATION: BARE-METAL MMAP TELEMETRY ENGINE
// ============================================================================

// ⚡ FIXED: Explicit types added, stray semicolon dropped, variables aligned!
EXPORT int append_mmap_telemetry_record(float cpu, uint32_t total_mem, uint32_t avail_mem) {
    if (!mmap_ring_buffer) return -1;

    // Fast bitmask boundary wrapping operation to enforce our 1024-slot ring limits
    uint32_t current_slot = active_log_index % MAX_STORAGE_RECORDS;

    // Write properties directly to our virtual memory address without file I/O blocks!
    mmap_ring_buffer[current_slot].timestamp = (uint64_t)time(NULL);
    mmap_ring_buffer[current_slot].cpu_usage = cpu;
    mmap_ring_buffer[current_slot].mem_total_mb = total_mem;
    mmap_ring_buffer[current_slot].mem_avail_mb = avail_mem;

    // Optional: Lazily hint the kernel subsystem to sync this modified memory page area down asynchronously
    // msync(&mmap_ring_buffer[current_slot], sizeof(TelemetryRecord), MS_ASYNC);

    active_log_index++;
    return (int)current_slot;
}

// Stream the raw stored records straight out back into memory arrays
int read_all_telemetry_records(TelemetryRecord* out_buffer, int max_records) {
    FILE* file = fopen(STORAGE_FILE_PATH, "rb");
    if (!file) return -1;

    fseek(file, 0, SEEK_SET);
    int records_read = (int)fread(out_buffer, sizeof(TelemetryRecord), max_records, file);

    fclose(file);
    return records_read;
}

// ============================================================================
// 🗄️ BARE-METAL DATABASE SCHEMA METADATA SCANNER CORE
// ============================================================================
EXPORT Response scan_database_schema(const char* db_path) {
    printf("[C-Core Storage] Scanning schema definitions for database: %s\n", db_path);
    Response res = {0, NULL};

    sqlite3* db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Could not acquire system handle lock on target database.\"}");
        return res;
    }

    // 2. Carve out our 2MB arena pool block to accumulate schema string tokens
    size_t scan_arena_size = 2 * 1024 * 1024;
    MemoryArena* scan_arena = arena_init(scan_arena_size);
    if (!scan_arena) {
        sqlite3_close(db);
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Out of memory panic during schema scanner arena instantiation.\"}");
        return res;
    }

    // Allocate three distinct json strings from our clean arena track
    char* tables_json = (char*)arena_alloc(scan_arena, 4096);
    char* views_json = (char*)arena_alloc(scan_arena, 4096);
    strcpy(tables_json, "[");
    strcpy(views_json, "[");

    int table_count = 0, view_count = 0;
    sqlite3_stmt* stmt = NULL;

    // Target the sqlite_master system catalog directly to scan structures
    const char* schema_query = "SELECT type, name FROM sqlite_master WHERE type IN ('table', 'view') AND name NOT LIKE 'sqlite_%';";

    if (sqlite3_prepare_v2(db, schema_query, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
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
        res.payload = strdup("{\"error\": \"Heap allocation fault for final schema JSON payload.\"}");
    }

    // Clean up all resources cleanly
    arena_destroy(scan_arena);
    sqlite3_close(db);

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
            res.payload = strdup("{\"error\": \"Failed to allocate system environment mapping arena.\"}");
            return res;
        }
    }

    // Allocate a pristine configuration chunk track inside our dedicated arena block boundaries
    EnvironmentBlock* new_config = (EnvironmentBlock*)arena_alloc(global_env_arena, sizeof(EnvironmentBlock));
    if (!new_config) {
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Environment manager arena capacity exhaustion fault.\"}");
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

// ============================================================================
// 🗄️ SYSTEMS OPTIMIZATION: BARE-METAL MMAP TELEMETRY ENGINE
// ============================================================================
EXPORT int init_mmap_telemetry_log(void) {
    printf("[C-Core Storage] Initializing virtual memory-mapped logging runway...\n");

    // 1. Open the telemetry log block descriptor with full read/write permissions
    shared_log_fd = open(STORAGE_FILE_PATH, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (shared_log_fd < 0) {
        perror("[C-Core Storage Error] Failed to secure log file descriptor");
        return -1;
    }

    // 2. Force a physical file allocation layout boundary sizing pass on disk
    if (ftruncate(shared_log_fd, MMAP_FILE_SIZE) != 0) {
        perror("[C-Core Storage Error] File system allocation sizing truncation failed");
        close(shared_log_fd);
        return -1;
    }

    // 3. Map the file descriptor directly into the process's virtual memory table bounds
    mmap_ring_buffer = (TelemetryRecord*)mmap(
        NULL,
        MMAP_FILE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        shared_log_fd,
        0
    );

    if (mmap_ring_buffer == MAP_FAILED) {
        perror("[C-Core Storage Error] Virtual memory mmap alignment sequence failed");
        close(shared_log_fd);
        return -1;
    }

    printf("[C-Core Storage] mmap allocation successful. Memory address linked to: %p\n", (void*)mmap_ring_buffer);
    return 0;
}

// ============================================================================
// 🗄️ SYSTEMS SUITE: METADATA SCHEMA SIDEBAR HARVESTER
// ============================================================================
EXPORT Response fetch_database_schema_tree(const char* db_path) {
    printf("[C-Core Storage] Harvesting master schema catalog logs from: %s\n", db_path);
    Response res = {0, NULL};

    sqlite3* db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Could not open database.\"}");
        return res;
    }

    const size_t json_capacity = 65536; // Increased capacity for complex schemas
    char* json_buffer = malloc(json_capacity);
    if (!json_buffer) {
        sqlite3_close(db);
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Out of memory.\"}");
        return res;
    }
    memset(json_buffer, 0, json_capacity);
    strncpy(json_buffer, "{\"tables\": [", json_capacity - 1);

    sqlite3_stmt *table_stmt = NULL, *col_stmt = NULL;
    const char* table_query = "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';";
    int table_count = 0;

    if (sqlite3_prepare_v2(db, table_query, -1, &table_stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(table_stmt) == SQLITE_ROW) {
            if (table_count > 0) strncat(json_buffer, ",", json_capacity - strlen(json_buffer) - 1);
            const char* table_name = (const char*)sqlite3_column_text(table_stmt, 0);
            
            char table_json[1024];
            snprintf(table_json, sizeof(table_json), "{\"name\": \"%s\", \"columns\": [", table_name);
            strncat(json_buffer, table_json, json_capacity - strlen(json_buffer) - 1);

            char pragma_query[256];
            snprintf(pragma_query, sizeof(pragma_query), "PRAGMA table_info(%s);", table_name);
            int col_count = 0;

            if (sqlite3_prepare_v2(db, pragma_query, -1, &col_stmt, NULL) == SQLITE_OK) {
                while (sqlite3_step(col_stmt) == SQLITE_ROW) {
                    if (col_count > 0) strncat(json_buffer, ",", json_capacity - strlen(json_buffer) - 1);
                    const char* col_name = (const char*)sqlite3_column_text(col_stmt, 1);
                    const char* col_type = (const char*)sqlite3_column_text(col_stmt, 2);
                    
                    char col_entry[512];
                    snprintf(col_entry, sizeof(col_entry), "\"%s (%s)\"", col_name, col_type);
                    strncat(json_buffer, col_entry, json_capacity - strlen(json_buffer) - 1);
                    col_count++;
                }
                sqlite3_finalize(col_stmt);
            }
            strncat(json_buffer, "]}", json_capacity - strlen(json_buffer) - 1);
            table_count++;
        }
        sqlite3_finalize(table_stmt);
    }

    strncat(json_buffer, "]}", json_capacity - strlen(json_buffer) - 1);
    sqlite3_close(db);

    res.status_code = 200;
    res.payload = json_buffer;
    return res;
}
