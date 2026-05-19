#ifndef LIGHTBASE_STORAGE_H
#define LIGHTBASE_STORAGE_H

#include <stdint.h>
#include <time.h>

#define EXPORT __attribute__((visibility("default")))
#define MAX_STORAGE_RECORDS 1024
#define STORAGE_FILE_PATH "lightbase_telemetry.log"
#define TLV_TAG_API_VERB     0x08
#define TLV_TAG_API_HEADERS  0x09
#define TLV_TAG_API_BODY     0x0A
#define BUFFER_SIZE 6144

// ============================================================================
// 📦 1. BASE SYSTEM CORE DATA STRUCTURES (DECLARED FIRST)
// ============================================================================

// Simple structure to match our global C-core data responses
typedef struct {
    int status_code;
    const char* payload;
} Response;

// Packed binary structural record layout (Exactly 32 Bytes - No Compiler Padding)
typedef struct __attribute__((packed)) {
    uint64_t timestamp;        // Epoch execution time tick
    float cpu_usage;           // Extracted kernel CPU percentage
    uint32_t mem_total_mb;     // Total system capacity footprint
    uint32_t mem_avail_mb;     // Live available RAM remaining
    uint8_t reserved[8];       // Strict alignment padding buffer for future proofing
} TelemetryRecord;

// Packed environment variable memory layout configuration block
typedef struct __attribute__((packed)) {
    char env_name[32];          // "Development", "Staging", "Production"
    char active_db_path[256];   // Target SQLite file path string
    uint32_t socket_timeout_ms; // Concurrency link boundary timer
    uint8_t security_level;     // 0 = Plain, 1 = Cryptographic Arena Locks
} EnvironmentBlock;


// ============================================================================
// 🎧 2. EXPORTED ENGINE FUNCTION SIGNATURES (DECLARED SECOND)
// ============================================================================

int init_log_ring_buffer(void);
int append_telemetry_record(float cpu, uint32_t total_mem, uint32_t avail_mem);
int read_all_telemetry_records(TelemetryRecord* out_buffer, int max_records);

EXPORT Response scan_database_schema(const char* db_path);
EXPORT Response load_studio_environment_state(const char* env_name, const char* db_path, uint32_t timeout, uint8_t sec_level);
EXPORT Response execute_studio_api_request(const char* verb, const char* host, const char* path, const char* headers, const char* body);

#endif // LIGHTBASE_STORAGE_H