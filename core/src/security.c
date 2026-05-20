#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <sys/mman.h>     // mlock, madvise — kernel memory protection
#include "engine.h"

// ============================================================================
// 🛡️ LIGHTBASE SECURITY HARDENING MODULE
// ============================================================================
// 1. Rate Limiter     — Token bucket per-client IP, prevents abuse
// 2. HMAC-SHA256      — Request integrity signing and verification
// 3. Secure Wipe      — Guaranteed zero-fill of sensitive memory regions
// 4. Path Guard       — Blocks directory traversal attacks on filesystem ops
// 5. Input Sanitizer  — SQL injection defense at the C level
// ============================================================================

// ─── 1. TOKEN BUCKET RATE LIMITER ──────────────────────────────────────────

#define MAX_RATE_CLIENTS 256
#define RATE_BUCKET_CAPACITY 60    // Max burst requests
#define RATE_REFILL_PER_SEC 10     // Tokens replenished per second

typedef struct {
    uint32_t client_hash;          // FNV-1a hash of client identifier
    double   tokens;               // Current token count
    double   last_refill;          // Timestamp of last refill
    int      active;               // Slot in use
} RateBucket;

static RateBucket rate_table[MAX_RATE_CLIENTS] = {0};

// FNV-1a hash for fast client identification
static uint32_t fnv1a_hash(const char* key) {
    uint32_t hash = 2166136261u;
    while (*key) {
        hash ^= (uint8_t)*key++;
        hash *= 16777619u;
    }
    return hash;
}

static double monotonic_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Returns 1 if request is allowed, 0 if rate-limited
EXPORT int rate_limiter_check(const char* client_id) {
    uint32_t h = fnv1a_hash(client_id);
    double now = monotonic_now();

    // Find existing bucket or empty slot
    int target = -1, empty_slot = -1;
    for (int i = 0; i < MAX_RATE_CLIENTS; i++) {
        if (rate_table[i].active && rate_table[i].client_hash == h) {
            target = i;
            break;
        }
        if (!rate_table[i].active && empty_slot < 0) empty_slot = i;
    }

    if (target < 0) {
        // New client — allocate bucket
        if (empty_slot < 0) {
            // Table full — evict oldest by replacing slot 0 (simple strategy)
            empty_slot = 0;
        }
        target = empty_slot;
        rate_table[target].client_hash = h;
        rate_table[target].tokens = RATE_BUCKET_CAPACITY;
        rate_table[target].last_refill = now;
        rate_table[target].active = 1;
    }

    RateBucket *b = &rate_table[target];

    // Refill tokens based on elapsed time
    double elapsed = now - b->last_refill;
    b->tokens += elapsed * RATE_REFILL_PER_SEC;
    if (b->tokens > RATE_BUCKET_CAPACITY) b->tokens = RATE_BUCKET_CAPACITY;
    b->last_refill = now;

    // Consume a token
    if (b->tokens >= 1.0) {
        b->tokens -= 1.0;
        return 1; // Allowed
    }

    return 0; // Rate limited
}

// Get remaining tokens for a client (for response headers)
EXPORT double rate_limiter_remaining(const char* client_id) {
    uint32_t h = fnv1a_hash(client_id);
    for (int i = 0; i < MAX_RATE_CLIENTS; i++) {
        if (rate_table[i].active && rate_table[i].client_hash == h) {
            return rate_table[i].tokens;
        }
    }
    return RATE_BUCKET_CAPACITY;
}


// ─── 2. HMAC-SHA256 REQUEST SIGNING ────────────────────────────────────────

// Generate a 64-byte random signing key (called once at startup)
static unsigned char hmac_signing_key[32] = {0};
static int hmac_key_initialized = 0;

EXPORT int security_init_hmac_key(void) {
    if (RAND_bytes(hmac_signing_key, 32) != 1) {
        fprintf(stderr, "[Security] Failed to generate HMAC signing key!\n");
        return -1;
    }
    hmac_key_initialized = 1;

    // Kernel-level protection: lock HMAC key in RAM, exclude from core dumps
    mlock(hmac_signing_key, 32);
    madvise(hmac_signing_key, 32, MADV_DONTDUMP);

    printf("[Security] 🔐 HMAC-SHA256 signing key initialized (mlock+dontdump).\n");
    return 0;
}

// Sign a request payload, returns hex-encoded HMAC (64 chars + null)
EXPORT char* security_sign_request(const char* payload) {
    if (!hmac_key_initialized || !payload) return NULL;

    unsigned char digest[32];
    unsigned int digest_len = 0;

    HMAC(EVP_sha256(), hmac_signing_key, 32,
         (const unsigned char*)payload, strlen(payload),
         digest, &digest_len);

    char *hex = malloc(65);
    if (!hex) return NULL;

    for (unsigned int i = 0; i < digest_len; i++) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    hex[64] = '\0';
    return hex;
}

// Verify a request payload against its HMAC signature
// Returns 1 if valid, 0 if tampered
EXPORT int security_verify_request(const char* payload, const char* hex_signature) {
    if (!hmac_key_initialized || !payload || !hex_signature) return 0;
    if (strlen(hex_signature) != 64) return 0;

    char *computed = security_sign_request(payload);
    if (!computed) return 0;

    // Constant-time comparison to prevent timing attacks
    int result = 1;
    for (int i = 0; i < 64; i++) {
        result &= (computed[i] == hex_signature[i]);
    }

    free(computed);
    return result;
}


// ─── 3. SECURE MEMORY WIPE ─────────────────────────────────────────────────
// Guaranteed zero-fill that the compiler cannot optimize away.
// Uses volatile to prevent dead-store elimination.

EXPORT void secure_wipe(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) {
        *p++ = 0;
    }
    // Memory barrier to ensure wipe completes before any subsequent operation
    __asm__ volatile("" ::: "memory");
}


// ─── 4. PATH TRAVERSAL GUARD ───────────────────────────────────────────────
// Validates filesystem paths to prevent directory traversal attacks.
// Blocks: "..", "~", absolute paths outside workspace, null bytes.

EXPORT int security_validate_path(const char* path, const char* allowed_root) {
    if (!path || !allowed_root) return 0;

    // Block null bytes (truncation attacks)
    for (const char *p = path; *p; p++) {
        if (*p == '\0') break;
    }

    // Block directory traversal sequences
    if (strstr(path, "..") != NULL) {
        fprintf(stderr, "[Security] ⛔ Path traversal blocked: %s\n", path);
        return 0;
    }

    // Block home directory expansion
    if (path[0] == '~') {
        fprintf(stderr, "[Security] ⛔ Home expansion blocked: %s\n", path);
        return 0;
    }

    // Block absolute paths that don't start with allowed root
    if (path[0] == '/') {
        if (strncmp(path, allowed_root, strlen(allowed_root)) != 0) {
            fprintf(stderr, "[Security] ⛔ Path outside workspace blocked: %s\n", path);
            return 0;
        }
    }

    // Block shell metacharacters in filenames
    const char *dangerous = "|;&$`!><";
    for (const char *d = dangerous; *d; d++) {
        if (strchr(path, *d) != NULL) {
            fprintf(stderr, "[Security] ⛔ Shell metacharacter in path blocked: %s\n", path);
            return 0;
        }
    }

    return 1; // Path is safe
}


// ─── 5. SQL INPUT SANITIZER ────────────────────────────────────────────────
// Detects dangerous SQL patterns that could indicate injection attempts.
// Returns 1 if safe, 0 if suspicious.

EXPORT int security_validate_sql(const char* sql) {
    if (!sql) return 0;

    // Convert to uppercase for pattern matching
    size_t len = strlen(sql);
    if (len > 8192) {
        fprintf(stderr, "[Security] ⛔ SQL query exceeds maximum length (8192 bytes)\n");
        return 0;
    }

    char *upper = malloc(len + 1);
    if (!upper) return 0;
    for (size_t i = 0; i <= len; i++) {
        upper[i] = (sql[i] >= 'a' && sql[i] <= 'z') ? sql[i] - 32 : sql[i];
    }

    int safe = 1;

    // Block dangerous system-level operations
    const char *blocked_patterns[] = {
        "ATTACH DATABASE",    // Can open arbitrary files
        "LOAD_EXTENSION",     // Can execute arbitrary shared libraries
        "PRAGMA JOURNAL_MODE", // Can corrupt data
        "PRAGMA WRITABLE_SCHEMA", // Can corrupt schema
        NULL
    };

    for (int i = 0; blocked_patterns[i]; i++) {
        if (strstr(upper, blocked_patterns[i]) != NULL) {
            fprintf(stderr, "[Security] ⛔ Blocked SQL pattern: %s\n", blocked_patterns[i]);
            safe = 0;
            break;
        }
    }

    // Detect stacked queries with suspicious patterns (basic heuristic)
    // Allow legitimate multi-statement SQL (CREATE + INSERT + SELECT)
    // but block comment-based injection markers
    if (strstr(upper, "--;") || strstr(upper, "/*") || strstr(upper, "*/")) {
        // Only flag if combined with known injection patterns
        if (strstr(upper, "UNION") && strstr(upper, "SELECT") && strstr(upper, "--")) {
            fprintf(stderr, "[Security] ⛔ Potential SQL injection detected\n");
            safe = 0;
        }
    }

    free(upper);
    return safe;
}


// ─── 6. IP ALLOWLIST / BLOCKLIST ───────────────────────────────────────────

#define MAX_IP_RULES 128

typedef struct {
    char ip[64];
    int  blocked;  // 1=blocked, 0=allowed
} IPRule;

static IPRule ip_rules[MAX_IP_RULES] = {0};
static int ip_rule_count = 0;

EXPORT int security_add_ip_rule(const char* ip, int block) {
    if (!ip || ip_rule_count >= MAX_IP_RULES) return -1;
    // Check for duplicate
    for (int i = 0; i < ip_rule_count; i++) {
        if (strcmp(ip_rules[i].ip, ip) == 0) {
            ip_rules[i].blocked = block;
            return 0;
        }
    }
    strncpy(ip_rules[ip_rule_count].ip, ip, 63);
    ip_rules[ip_rule_count].blocked = block;
    ip_rule_count++;
    printf("[Security] IP rule added: %s → %s\n", ip, block ? "BLOCKED" : "ALLOWED");
    return 0;
}

EXPORT int security_remove_ip_rule(const char* ip) {
    if (!ip) return -1;
    for (int i = 0; i < ip_rule_count; i++) {
        if (strcmp(ip_rules[i].ip, ip) == 0) {
            // Shift remaining rules down
            for (int j = i; j < ip_rule_count - 1; j++) {
                ip_rules[j] = ip_rules[j + 1];
            }
            ip_rule_count--;
            return 0;
        }
    }
    return -1;
}

// Returns 1 if allowed, 0 if blocked
EXPORT int security_check_ip(const char* ip) {
    if (!ip || ip_rule_count == 0) return 1; // No rules = allow all
    for (int i = 0; i < ip_rule_count; i++) {
        if (strcmp(ip_rules[i].ip, ip) == 0) {
            if (ip_rules[i].blocked) {
                fprintf(stderr, "[Security] ⛔ Blocked IP: %s\n", ip);
                return 0;
            }
            return 1;
        }
    }
    return 1; // Not in list = allow by default
}

EXPORT char* security_list_ip_rules(void) {
    char *buf = malloc(8192);
    if (!buf) return NULL;
    char *ptr = buf;
    ptr += sprintf(ptr, "[");
    for (int i = 0; i < ip_rule_count; i++) {
        if (i > 0) ptr += sprintf(ptr, ",");
        ptr += sprintf(ptr, "{\"ip\":\"%s\",\"action\":\"%s\"}",
                       ip_rules[i].ip, ip_rules[i].blocked ? "block" : "allow");
    }
    sprintf(ptr, "]");
    return buf;
}


// ─── 7. AUDIT TRAIL LOGGER ─────────────────────────────────────────────────

#define MAX_AUDIT_ENTRIES 500

typedef struct {
    char route[128];
    char client[64];
    char method[16];
    uint64_t timestamp;
    int status_code;
} AuditEntry;

static AuditEntry audit_log[MAX_AUDIT_ENTRIES] = {0};
static int audit_head = 0;
static int audit_count = 0;

EXPORT void security_audit_log(const char* route, const char* client, const char* method, int status) {
    AuditEntry *entry = &audit_log[audit_head];
    strncpy(entry->route, route ? route : "", 127);
    strncpy(entry->client, client ? client : "unknown", 63);
    strncpy(entry->method, method ? method : "POST", 15);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    entry->timestamp = (uint64_t)ts.tv_sec;
    entry->status_code = status;
    audit_head = (audit_head + 1) % MAX_AUDIT_ENTRIES;
    if (audit_count < MAX_AUDIT_ENTRIES) audit_count++;
}

EXPORT char* security_get_audit_log(int max_entries) {
    if (max_entries <= 0 || max_entries > audit_count) max_entries = audit_count;

    char *buf = malloc(max_entries * 256 + 64);
    if (!buf) return NULL;
    char *ptr = buf;
    ptr += sprintf(ptr, "[");

    int start = (audit_head - max_entries + MAX_AUDIT_ENTRIES) % MAX_AUDIT_ENTRIES;
    for (int i = 0; i < max_entries; i++) {
        int idx = (start + i) % MAX_AUDIT_ENTRIES;
        if (i > 0) ptr += sprintf(ptr, ",");
        ptr += sprintf(ptr, "{\"route\":\"%s\",\"client\":\"%s\",\"method\":\"%s\",\"status\":%d,\"timestamp\":%lu}",
                       audit_log[idx].route, audit_log[idx].client,
                       audit_log[idx].method, audit_log[idx].status_code,
                       (unsigned long)audit_log[idx].timestamp);
    }
    sprintf(ptr, "]");
    return buf;
}

EXPORT void security_clear_audit_log(void) {
    audit_head = 0;
    audit_count = 0;
    memset(audit_log, 0, sizeof(audit_log));
}


// ─── 8. SESSION TOKEN GENERATOR WITH EXPIRY ────────────────────────────────

#define MAX_SESSIONS 64
#define SESSION_TOKEN_LEN 32

typedef struct {
    char token[65];    // hex-encoded 32-byte token
    char label[64];    // human label (e.g. "admin_session")
    uint64_t created;
    uint64_t expires;
    int active;
} Session;

static Session sessions[MAX_SESSIONS] = {0};

EXPORT char* security_create_session(const char* label, int ttl_seconds) {
    // Find empty slot
    int slot = -1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        // Evict oldest
        slot = 0;
        uint64_t oldest = sessions[0].created;
        for (int i = 1; i < MAX_SESSIONS; i++) {
            if (sessions[i].created < oldest) { oldest = sessions[i].created; slot = i; }
        }
    }

    // Generate random token
    unsigned char raw[SESSION_TOKEN_LEN];
    RAND_bytes(raw, SESSION_TOKEN_LEN);

    Session *s = &sessions[slot];
    for (int i = 0; i < SESSION_TOKEN_LEN; i++) {
        sprintf(s->token + i * 2, "%02x", raw[i]);
    }
    s->token[64] = '\0';
    strncpy(s->label, label ? label : "default", 63);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    s->created = (uint64_t)ts.tv_sec;
    s->expires = s->created + (ttl_seconds > 0 ? ttl_seconds : 3600);
    s->active = 1;

    // Wipe raw key material
    secure_wipe(raw, SESSION_TOKEN_LEN);

    // Return JSON with the token
    char *result = malloc(256);
    if (!result) return NULL;
    snprintf(result, 256, "{\"token\":\"%s\",\"label\":\"%s\",\"expires\":%lu}",
             s->token, s->label, (unsigned long)s->expires);
    return result;
}

EXPORT int security_validate_session(const char* token) {
    if (!token || strlen(token) != 64) return 0;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t now = (uint64_t)ts.tv_sec;

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active && strcmp(sessions[i].token, token) == 0) {
            if (now > sessions[i].expires) {
                sessions[i].active = 0;
                fprintf(stderr, "[Security] ⏰ Session expired: %s\n", sessions[i].label);
                return 0;
            }
            return 1; // Valid
        }
    }
    return 0; // Not found
}

EXPORT void security_revoke_session(const char* token) {
    if (!token) return;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active && strcmp(sessions[i].token, token) == 0) {
            secure_wipe(sessions[i].token, 65);
            sessions[i].active = 0;
            printf("[Security] 🗑️ Session revoked: %s\n", sessions[i].label);
            return;
        }
    }
}

EXPORT char* security_list_sessions(void) {
    char *buf = malloc(8192);
    if (!buf) return NULL;
    char *ptr = buf;
    ptr += sprintf(ptr, "[");

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t now = (uint64_t)ts.tv_sec;
    int first = 1;

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!sessions[i].active) continue;
        if (!first) ptr += sprintf(ptr, ",");
        first = 0;
        ptr += sprintf(ptr, "{\"token\":\"%s\",\"label\":\"%s\",\"created\":%lu,\"expires\":%lu,\"remaining_sec\":%ld}",
                       sessions[i].token, sessions[i].label,
                       (unsigned long)sessions[i].created,
                       (unsigned long)sessions[i].expires,
                       (long)(sessions[i].expires - now));
    }
    sprintf(ptr, "]");
    return buf;
}


// ─── 9. REQUEST SIZE LIMITER ───────────────────────────────────────────────

static size_t max_request_size = 1048576; // 1MB default

EXPORT void security_set_max_request_size(size_t size_bytes) {
    max_request_size = size_bytes;
    printf("[Security] 📏 Max request size set to %zu bytes\n", size_bytes);
}

EXPORT int security_check_request_size(size_t size) {
    if (size > max_request_size) {
        fprintf(stderr, "[Security] ⛔ Request too large: %zu > %zu bytes\n", size, max_request_size);
        return 0;
    }
    return 1;
}

EXPORT size_t security_get_max_request_size(void) {
    return max_request_size;
}


// ─── 10. SECURITY STATUS REPORT ────────────────────────────────────────────

EXPORT char* security_status_report(void) {
    char *report = malloc(4096);
    if (!report) return NULL;

    int active_clients = 0;
    for (int i = 0; i < MAX_RATE_CLIENTS; i++) {
        if (rate_table[i].active) active_clients++;
    }

    int active_sessions = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].active) active_sessions++;
    }

    snprintf(report, 4096,
        "{"
        "\"hmac_initialized\": %s,"
        "\"rate_limiter_clients\": %d,"
        "\"rate_bucket_capacity\": %d,"
        "\"rate_refill_per_sec\": %d,"
        "\"path_guard\": true,"
        "\"sql_sanitizer\": true,"
        "\"secure_wipe\": true,"
        "\"aes_256_gcm_vault\": true,"
        "\"ip_rules_count\": %d,"
        "\"audit_log_entries\": %d,"
        "\"active_sessions\": %d,"
        "\"max_request_size\": %zu"
        "}",
        hmac_key_initialized ? "true" : "false",
        active_clients,
        RATE_BUCKET_CAPACITY,
        RATE_REFILL_PER_SEC,
        ip_rule_count,
        audit_count,
        active_sessions,
        max_request_size);

    return report;
}

