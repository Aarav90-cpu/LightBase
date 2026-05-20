#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    int status_code;
    const char* payload;
} Response;

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

// ============================================================================
// 🌐 CORE ENGINE EXPORTED FUNCTION SIGNATURES
// ============================================================================

// 1. Core IPC and Database Engine
EXPORT Response execute_raw_query(const char* query);
EXPORT Response fire_http_get(const char* method, const char* hostname, const char* path, const char* headers, const char* body, const char* form_data);
EXPORT Response execute_local_db(const char* db_path, const char* sql_query);
EXPORT int start_linux_ipc_bridge(void);

// 2. Thread Pool Management
EXPORT int initialize_c_core_interceptor_pool(void);

// 3. QuickJS Assertion Engine
EXPORT char* execute_native_quickjs_assert_suite(const char* test_script, const char* response_json_str);

// 4. Documentation and Schema Generators
EXPORT char* compile_native_markdown_schema_spec(const char* db_path);

// 5. Git Repository Introspection
EXPORT char* compile_native_git_branch_status(const char* repo_path);

// 6. Local AI Inference Engine
EXPORT char* execute_local_ai_inference_stream(const char* prompt_context);

// 7. Crypto Vault (AES-256-GCM)
EXPORT char* encrypt_api_key_system_level(const char* plain_text_key);
EXPORT char* decrypt_api_key_system_level(const char* hex_encoded_token);

// 8. Collections Ledger
EXPORT char* list_all_collections(const char* db_path);

// 9. Reactive Git-State Interceptor Engine
EXPORT int start_git_reactive_watcher(const char* repo_path);
EXPORT char* poll_git_reactive_state(void);
EXPORT void ack_git_reactive_event(void);

// 10. Lock-Free Pool Telemetry
void get_pool_telemetry(uint64_t *enqueued, uint64_t *processed, uint64_t *spins);

// 11. Security Hardening Module
EXPORT int security_init_hmac_key(void);
EXPORT char* security_sign_request(const char* payload);
EXPORT int security_verify_request(const char* payload, const char* hex_signature);
EXPORT void secure_wipe(void* ptr, size_t len);
EXPORT int security_validate_path(const char* path, const char* allowed_root);
EXPORT int security_validate_sql(const char* sql);
EXPORT int rate_limiter_check(const char* client_id);
EXPORT double rate_limiter_remaining(const char* client_id);
EXPORT char* security_status_report(void);

#endif // ENGINE_H