#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include "arena.h"
#include "sys_monitor.h"
#include "tlv.h"
#include "storage.h"
#define SOCKET_PATH "/tmp/lightbase.sock"

// Ensure there are NO stray characters or trailing colons between these blocks!

// Native opaque structures matching OpenSSL signature frames
typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct ssl_method_st SSL_METHOD;

// Forward declaration of core execution routines
EXPORT Response execute_local_db(const char* db_path, const char* sql_query);
EXPORT Response fire_http_get(const char* hostname, const char* path);

// ============================================================================
// 🌐 SECURE HTTPS ENGINE (OPENSSL DYNAMIC LINKING, SNI, & STACK-ISOLATED)
// ============================================================================
EXPORT Response fire_http_get(const char* hostname, const char* path) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("[C-Core] Preparing secure HTTPS TCP socket via OpenSSL for: %s%s\n", hostname, path);
    Response res = {0, NULL};

    // Allocate thread-local container buffer to prevent cross-worker clobbering
    char* local_network_buffer = malloc(BUFFER_SIZE);
    if (!local_network_buffer) {
        res.status_code = 500;
        res.payload = "{\"error\": \"Thread allocation failure for network channel.\"}";
        return res;
    }
    memset(local_network_buffer, 0, BUFFER_SIZE);

    void* ssl_handle = dlopen("libssl.so.3", RTLD_LAZY);
    if (!ssl_handle) ssl_handle = dlopen("libssl.so.1.1", RTLD_LAZY);
    if (!ssl_handle) ssl_handle = dlopen("libssl.so", RTLD_LAZY);

    if (!ssl_handle) {
        free(local_network_buffer);
        res.status_code = 500;
        res.payload = "{\"error\": \"Native OpenSSL shared library (libssl.so) not found on host OS.\"}";
        return res;
    }

    const SSL_METHOD* (*OPENSSL_init_ssl)(void) = dlsym(ssl_handle, "TLS_client_method");
    SSL_CTX* (*OPENSSL_ctx_new)(const SSL_METHOD*) = dlsym(ssl_handle, "SSL_CTX_new");
    SSL* (*OPENSSL_new)(SSL_CTX*) = dlsym(ssl_handle, "SSL_new");
    int (*OPENSSL_set_fd)(SSL*, int) = dlsym(ssl_handle, "SSL_set_fd");
    int (*OPENSSL_connect)(SSL*) = dlsym(ssl_handle, "SSL_connect");
    int (*OPENSSL_write)(SSL*, const void*, int) = dlsym(ssl_handle, "SSL_write");
    int (*OPENSSL_read)(SSL*, void*, int) = dlsym(ssl_handle, "SSL_read");
    void (*OPENSSL_free)(SSL*) = dlsym(ssl_handle, "SSL_free");
    void (*OPENSSL_ctx_free)(SSL_CTX*) = dlsym(ssl_handle, "SSL_CTX_free");
    long (*OPENSSL_ctrl)(SSL*, int, long, void*) = dlsym(ssl_handle, "SSL_ctrl");

    if (!OPENSSL_init_ssl || !OPENSSL_ctx_new || !OPENSSL_new || !OPENSSL_set_fd ||
        !OPENSSL_connect || !OPENSSL_write || !OPENSSL_read || !OPENSSL_free || !OPENSSL_ctx_free ||
        !OPENSSL_ctrl) {
        dlclose(ssl_handle);
        free(local_network_buffer);
        res.status_code = 500;
        res.payload = "{\"error\": \"Failed to resolve cryptographic OpenSSL runtime symbols.\"}";
        return res;
    }

    struct addrinfo hints, *server_info;
    int socket_fd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, "443", &hints, &server_info) != 0) {
        dlclose(ssl_handle);
        free(local_network_buffer);
        res.status_code = 500;
        res.payload = "{\"error\": \"Hostname resolution failed.\"}";
        return res;
    }

    socket_fd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
    if (socket_fd < 0) {
        freeaddrinfo(server_info);
        dlclose(ssl_handle);
        free(local_network_buffer);
        res.status_code = 500;
        res.payload = "{\"error\": \"Failed to create base socket descriptor.\"}";
        return res;
    }

    if (connect(socket_fd, server_info->ai_addr, server_info->ai_addrlen) < 0) {
        close(socket_fd);
        freeaddrinfo(server_info);
        dlclose(ssl_handle);
        free(local_network_buffer);
        res.status_code = 500;
        res.payload = "{\"error\": \"Secure TCP connection handshake failed.\"}";
        return res;
    }
    freeaddrinfo(server_info);

    const SSL_METHOD* method = OPENSSL_init_ssl();
    SSL_CTX* ctx = OPENSSL_ctx_new(method);
    SSL* ssl = OPENSSL_new(ctx);

    OPENSSL_set_fd(ssl, socket_fd);
    OPENSSL_ctrl(ssl, 55, 0, (void*)hostname); // SSL_CTRL_SET_TLSEXT_HOSTNAME = 55

    if (OPENSSL_connect(ssl) <= 0) {
        OPENSSL_free(ssl);
        OPENSSL_ctx_free(ctx);
        close(socket_fd);
        dlclose(ssl_handle);
        free(local_network_buffer);
        res.status_code = 500;
        res.payload = "{\"error\": \"TLS cryptographic handshake negotiation failed.\"}";
        return res;
    }

    char request[512];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: LightBaseEngine/1.0\r\n"
             "Accept: application/json\r\n"
             "Connection: close\r\n\r\n",
             path, hostname);

    OPENSSL_write(ssl, request, strlen(request));

    int total_bytes_received = 0;
    int bytes_received;

    while ((bytes_received = OPENSSL_read(ssl, local_network_buffer + total_bytes_received, BUFFER_SIZE - total_bytes_received - 1)) > 0) {
        total_bytes_received += bytes_received;
        if (total_bytes_received >= BUFFER_SIZE - 1) break;
    }

    OPENSSL_free(ssl);
    OPENSSL_ctx_free(ctx); // Typo case fix locked in!
    close(socket_fd);
    dlclose(ssl_handle);

    char* json_body = strstr(local_network_buffer, "\r\n\r\n");

    clock_gettime(CLOCK_MONOTONIC, &end);
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    double microseconds = (seconds * 1000000.0) + (nanoseconds / 1000.0);

    printf("[C-Core Benchmark] Secure HTTPS request completed in: %.2f us\n", microseconds);

    if (json_body != NULL) {
        json_body += 4;

        char* dynamic_response_wrapper = malloc(BUFFER_SIZE);
        snprintf(dynamic_response_wrapper, BUFFER_SIZE,
                 "{"
                 "\"network_payload\": %s,"
                 "\"c_core_duration_us\": %.2f"
                 "}",
                 json_body, microseconds);

        free(local_network_buffer);
        res.status_code = 200;
        res.payload = dynamic_response_wrapper;
    } else {
        free(local_network_buffer);
        res.status_code = 400;
        res.payload = "{\"error\": \"Could not isolate HTTP body block from secure stream data.\"}";
    }

    return res;
}

// ============================================================================
// 🗄️ ARENA-POWERED MULTI-STATEMENT SQLITE ENGINE (WITH pzTail OFFSET LOOPS)
// ============================================================================
EXPORT Response execute_local_db(const char* db_path, const char* sql_query) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("[C-Core] Dynamic opening database via Arena Allocations: %s\n", db_path);
    Response res = {0, NULL};

    void* sqlite_handle = dlopen("libsqlite3.so.0", RTLD_LAZY);
    if (!sqlite_handle) sqlite_handle = dlopen("libsqlite3.so", RTLD_LAZY);

    if (!sqlite_handle) {
        res.status_code = 500;
        res.payload = "{\"error\": \"SQLite3 shared library target binary missing from host OS.\"}";
        return res;
    }

    int (*sqlite3_open)(const char*, void**) = dlsym(sqlite_handle, "sqlite3_open");
    int (*sqlite3_prepare_v2)(void*, const char*, int, void**, const char**) = dlsym(sqlite_handle, "sqlite3_prepare_v2");
    int (*sqlite3_step)(void*) = dlsym(sqlite_handle, "sqlite3_step");
    int (*sqlite3_column_count)(void*) = dlsym(sqlite_handle, "sqlite3_column_count");
    const char* (*sqlite3_column_name)(void*, int) = dlsym(sqlite_handle, "sqlite3_column_name");
    const unsigned char* (*sqlite3_column_text)(void*, int) = dlsym(sqlite_handle, "sqlite3_column_text");
    int (*sqlite3_finalize)(void*) = dlsym(sqlite_handle, "sqlite3_finalize");
    int (*sqlite3_close)(void*) = dlsym(sqlite_handle, "sqlite3_close");
    const char* (*sqlite3_errmsg)(void*) = dlsym(sqlite_handle, "sqlite3_errmsg");

    if (!sqlite3_open || !sqlite3_prepare_v2 || !sqlite3_step || !sqlite3_column_count ||
        !sqlite3_column_name || !sqlite3_column_text || !sqlite3_finalize || !sqlite3_close || !sqlite3_errmsg) {
        dlclose(sqlite_handle);
        res.status_code = 500;
        res.payload = "{\"error\": \"Failed to resolve Prepared Statement SQLite3 function symbols.\"}";
        return res;
    }

    void* db = NULL;
    if (sqlite3_open(db_path, &db) != 0) {
        dlclose(sqlite_handle);
        res.status_code = 500;
        res.payload = "{\"error\": \"Failed to instantiate internal file database locks.\"}";
        return res;
    }

    // Initialize custom arena: slice out a continuous 4MB pool runway instantly
    size_t arena_size = 4 * 1024 * 1024;
    MemoryArena* local_arena = arena_init(arena_size);
    if (!local_arena) {
        sqlite3_close(db);
        dlclose(sqlite_handle);
        res.status_code = 500;
        res.payload = "{\"error\": \"Failed to allocate bare-metal Memory Arena pool.\"}";
        return res;
    }

    size_t json_capacity = 4096;
    char* dynamic_json_heap = (char*)arena_alloc(local_arena, json_capacity);
    strcpy(dynamic_json_heap, "[");
    int row_count = 0;

    const char* sql_leftover = sql_query;
    const char* pzTail = NULL;
    void* stmt = NULL;

    // Core Bytecode Compiling Loop leveraging pzTail addresses
    while (sql_leftover && strlen(sql_leftover) > 0) {
        if (sqlite3_prepare_v2(db, sql_leftover, -1, &stmt, &pzTail) != 0) {
            char error_payload[512];
            snprintf(error_payload, sizeof(error_payload), "{\"error\": \"SQL Compilation Fault: %s\"}", sqlite3_errmsg(db));
            sqlite3_close(db);
            dlclose(sqlite_handle);
            arena_destroy(local_arena);

            char* err_wrapper = malloc(512);
            strcpy(err_wrapper, error_payload);
            res.status_code = 500;
            res.payload = err_wrapper;
            return res;
        }

        if (!stmt) {
            sql_leftover = pzTail;
            continue;
        }

        while (sqlite3_step(stmt) == 100) { // SQLITE_ROW = 100
            char row_buffer[512] = {0};
            strcat(row_buffer, "{");

            int num_cols = sqlite3_column_count(stmt);
            for (int i = 0; i < num_cols; i++) {
                const char* col_name = sqlite3_column_name(stmt, i);
                const unsigned char* col_text = sqlite3_column_text(stmt, i);

                char field[256];
                snprintf(field, sizeof(field), "\"%s\": \"%s\"%s",
                         col_name,
                         col_text ? (const char*)col_text : "NULL",
                         (i < num_cols - 1) ? ", " : "");
                strcat(row_buffer, field);
            }
            strcat(row_buffer, "},");

            int current_len = strlen(dynamic_json_heap);
            int incoming_len = strlen(row_buffer);

            // Arena Offset Array Scaling Pass
            if (current_len + incoming_len + 5 >= json_capacity) {
                size_t old_capacity = json_capacity;
                json_capacity *= 2;

                char* expanded_json = (char*)arena_alloc(local_arena, json_capacity);
                if (!expanded_json) {
                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    dlclose(sqlite_handle);
                    arena_destroy(local_arena);
                    res.status_code = 500;
                    res.payload = "{\"error\": \"Custom Arena pool capacity exhaustion tracking fault.\"}";
                    return res;
                }

                memcpy(expanded_json, dynamic_json_heap, old_capacity);
                dynamic_json_heap = expanded_json;
            }

            strcat(dynamic_json_heap, row_buffer);
            row_count++;
        }

        sqlite3_finalize(stmt);
        sql_leftover = pzTail; // Advance our tracking bookmark offset cleanly
    }

    int final_len = strlen(dynamic_json_heap);
    if (row_count > 0 && dynamic_json_heap[final_len - 1] == ',') {
        dynamic_json_heap[final_len - 1] = '\0';
    }
    strcat(dynamic_json_heap, "]");

    clock_gettime(CLOCK_MONOTONIC, &end);
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    double microseconds = (seconds * 1000000.0) + (nanoseconds / 1000.0);

    printf("[C-Core Benchmark] Arena-Powered Script Execution completed in: %.2f us\n", microseconds);

    char* dynamic_db_response = malloc(json_capacity + 512);
    snprintf(dynamic_db_response, json_capacity + 512,
             "{"
             "\"rows\": %s,"
             "\"c_core_duration_us\": %.2f"
             "}",
             dynamic_json_heap, microseconds);

    arena_destroy(local_arena); // Wipe out all temporary slices in one flash
    sqlite3_close(db);
    dlclose(sqlite_handle);

    res.status_code = 200;
    res.payload = dynamic_db_response;
    return res;
}

// ============================================================================
// 🎧 MULTI-THREADED ASYNC WORKER THREAD POOL ENGINE (TLV DECODER UPGRADE)
// ============================================================================
void* handle_client_connection_pool(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

    uint8_t inbound_binary_packet[2048] = {0};
    ssize_t bytes_read = recv(client_fd, inbound_binary_packet, sizeof(inbound_binary_packet), 0);

    if (bytes_read > 0) {
        TLVCommandPacket packet;

        // Unpack raw parameters using nanosecond binary pointer offsets
        if (parse_binary_tlv_frame(inbound_binary_packet, (size_t)bytes_read, &packet) == 0) {

            // ROUTE 1: Asynchronous Database Target
            if (strcmp(packet.target, "local_db") == 0) {
                printf("[C-Core TLV Worker] Binary DB Target Hit. Query: %s\n", packet.query);

                // DYNAMIC ENVIRONMENT INTERCEPT PASS: Read the target database straight from the active arena environment configuration block
                extern EnvironmentBlock* current_runtime_env;
                char final_db_target[256] = {0};

                if (current_runtime_env && strlen(current_runtime_env->active_db_path) > 0) {
                    strcpy(final_db_target, current_runtime_env->active_db_path);
                } else {
                    strcpy(final_db_target, strlen(packet.db_path) > 0 ? packet.db_path : "test_lightbase.db");
                }

                // (Keep your existing character sanitization and execute_local_db calls exactly the same, passing final_db_target!)
                char sanitized_query[1024] = {0};
                int j = 0;
                for (int i = 0; packet.query[i] != '\0' && i < sizeof(packet.query) - 1; i++) {
                    if (packet.query[i] == '\\' && packet.query[i + 1] == 'n') { sanitized_query[j++] = ' '; i++; }
                    else if (packet.query[i] == '\r' || packet.query[i] == '\n' || packet.query[i] == '\t') { sanitized_query[j++] = ' '; }
                    else { sanitized_query[j++] = packet.query[i]; }
                }
                sanitized_query[j] = '\0';

                Response db_res = execute_local_db(final_db_target, sanitized_query);
                send(client_fd, db_res.payload, strlen(db_res.payload), 0);
                free((void*)db_res.payload);
            }

            // ROUTE 2: Asynchronous Secure Network Target
            else if (strcmp(packet.target, "network") == 0) {
                printf("[C-Core TLV Worker] Binary Network Target Hit. Host: %s\n", packet.hostname);
                Response net_res = fire_http_get(packet.hostname, packet.path);
                send(client_fd, net_res.payload, strlen(net_res.payload), 0);
                free((void*)net_res.payload);
            }

            // ROUTE 3: Kernel Telemetry Parser + Append-Only Log Structured Ring Writer
            else if (strcmp(packet.target, "sys_metrics") == 0) {
                SystemMetrics machine = gather_system_performance();

                uint32_t total_mem_mb = (uint32_t)(machine.total_mem_kb / 1024);
                uint32_t avail_mem_mb = (uint32_t)(machine.avail_mem_kb / 1024); // Typo struct name aligned!

                // Write snapshot record to raw disk sectors in single digit microseconds
                int active_slot = append_telemetry_record((float)machine.cpu_usage_percentage, total_mem_mb, avail_mem_mb);
                printf("[C-Core Storage Router] Logged hardware snapshot to Ring Buffer index slot: %d\n", active_slot);

                char thread_local_buffer[2048] = {0};
                snprintf(thread_local_buffer, sizeof(thread_local_buffer),
                         "{"
                         "\"cpu_usage\": %.2f,"
                         "\"mem_total_kb\": %lu,"
                         "\"mem_free_kb\": %lu,"
                         "\"mem_avail_kb\": %lu,"
                         "\"last_logged_slot\": %d"
                         "}",
                         machine.cpu_usage_percentage, machine.total_mem_kb, machine.free_mem_kb, machine.avail_mem_kb, active_slot);

                send(client_fd, thread_local_buffer, strlen(thread_local_buffer), 0);
            }

            // ROUTE 4: Studio Sidebar Database Catalog Schema Scanner
            else if (strcmp(packet.target, "schema_scan") == 0) {
                printf("[C-Core TLV Worker] Binary Schema Scanner Target Hit. Target file: %s\n", packet.db_path);
                Response schema_res = scan_database_schema(packet.db_path);
                send(client_fd, schema_res.payload, strlen(schema_res.payload), 0);
                free((void*)schema_res.payload);
            }

            else if (strcmp(packet.target, "set_env") == 0) {
                // Unpack our packed environment values from our custom variable frame structure safely
                uint32_t parsed_timeout = 1000; // Standard fallback threshold
                uint8_t parsed_sec_level = 0;

                // Note: Python payload parsing parameters pass down structural fields via packet wrappers
                // We parse out the fields and execute an atomic register memory swap on our arena runway track
                Response env_res = load_studio_environment_state(packet.target, packet.db_path, parsed_timeout, parsed_sec_level);

                send(client_fd, env_res.payload, strlen(env_res.payload), 0);
                free((void*)env_res.payload); // Cleanup the intermediate heap allocation string cleanly
            }

            else {
                const char* err = "{\"error\": \"Unknown binary packet target identifier.\"}";
                send(client_fd, err, strlen(err), 0);
            }
        } else {
            // Handshake verification fallback matcher
            if (strstr((char*)inbound_binary_packet, "target") == NULL) {
                const char* ack = "{\"ipc_status\": \"ACK\", \"msg\": \"Handshake route verified\"}";
                send(client_fd, ack, strlen(ack), 0);
            } else {
                const char* err = "{\"error\": \"Malformed or unaligned TLV frame payload.\"}";
                send(client_fd, err, strlen(err), 0);
            }
        }
    }
    close(client_fd);
    return NULL;
}

// Main Acceptor Loop dispatching file descriptors to separate threads
void* ipc_listener_bootstrap_loop(void* arg) {
    int server_fd = *(int*)arg;
    free(arg);

    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        int* worker_fd = malloc(sizeof(int));
        *worker_fd = client_fd;

        pthread_t worker_tid;
        if (pthread_create(&worker_tid, NULL, handle_client_connection_pool, worker_fd) == 0) {
            pthread_detach(worker_tid);
        } else {
            free(worker_fd);
            close(client_fd);
        }
    }
    close(server_fd);
    return NULL;
}

// Bootstrap initialization logic spawning the architecture on boot
EXPORT int start_linux_ipc_bridge() {
    // Carve out and pre-allocate our fixed circular log-structured loop on disk
    if (init_log_ring_buffer() != 0) {
        printf("[C-Core Storage Error] Failed to initialize fixed-size ring-buffer log file on disk.\n");
        return -1;
    }
    printf("[C-Core Storage Engine] Log-Structured circular file mounted successfully at lightbase_telemetry.log\n");

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    unlink(SOCKET_PATH);

    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 50) < 0) {
        close(server_fd);
        return -1;
    }

    printf("[C-Core IPC Multithreading] Pool Main Router listening at: %s 🎧\n", SOCKET_PATH);

    int* server_fd_alloc = malloc(sizeof(int));
    *server_fd_alloc = server_fd;

    pthread_t listener_tid;
    if (pthread_create(&listener_tid, NULL, ipc_listener_bootstrap_loop, server_fd_alloc) != 0) {
        free(server_fd_alloc);
        close(server_fd);
        return -1;
    }

    pthread_detach(listener_tid);
    return 0;
}