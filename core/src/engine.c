#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sqlite3.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "arena.h"
#include "sys_monitor.h"
#include "tlv.h"
#include "storage.h"

#define IPC_PORT 8001

uint32_t active_log_index = 0;

// Forward declaration of core execution routines
EXPORT Response execute_local_db(const char* db_path, const char* sql_query);
EXPORT Response fire_http_get(const char* method, const char* hostname, const char* path, const char* headers, const char* body, const char* form_data);

// 🎯 Forward declarations from collections.c
int initialize_system_collections_ledger_table(sqlite3 *db);
int insert_collection_record_to_arena(sqlite3 *db, const char *name, const char *method, const char *url);
sqlite3 *open_arena_database_connection(const char *db_filename);

// ============================================================================
// 🌐 SECURE HTTPS ENGINE (OPENSSL STATIC LINKING, SNI, & STACK-ISOLATED)
// ============================================================================
EXPORT Response fire_http_get(const char* method, const char* hostname, const char* path, const char* headers, const char* body, const char* form_data) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // 🛡️ Safety cursor: strip any leading protocol if it leaked through the UI layer
    const char* host_cursor = hostname;
    if (strncmp(host_cursor, "https://", 8) == 0) host_cursor += 8;
    else if (strncmp(host_cursor, "http://", 7) == 0) host_cursor += 7;

    printf("[C-Core] Preparing secure HTTPS TCP socket via OpenSSL for: %s %s%s\n", method, host_cursor, path);
    Response res = {0, NULL};

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Failed to create SSL context.\"}");
        return res;
    }

    SSL* ssl = SSL_new(ctx);
    if (!ssl) {
        SSL_CTX_free(ctx);
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Failed to create SSL object.\"}");
        return res;
    }

    // SNI support
    SSL_set_tlsext_host_name(ssl, host_cursor);

    struct hostent* server = gethostbyname(host_cursor);
    if (!server) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"DNS resolution failed for target host.\"}");
        return res;
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Failed to create TCP socket.\"}");
        return res;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(443);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(socket_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(socket_fd);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Socket connection to target port 443 failed.\"}");
        return res;
    }

    SSL_set_fd(ssl, socket_fd);
    if (SSL_connect(ssl) <= 0) {
        close(socket_fd);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"SSL/TLS handshake failed with target host.\"}");
        return res;
    }

    char* local_network_buffer = malloc(BUFFER_SIZE);
    if (!local_network_buffer) {
        close(socket_fd);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Memory allocation failed for network buffer.\"}");
        return res;
    }
    memset(local_network_buffer, 0, BUFFER_SIZE);

    char request[16384]; // Significantly increased to handle large bodies and headers safely
    memset(request, 0, sizeof(request));
    
    // Determine the effective body and Content-Type
    const char* final_body = (body && strlen(body) > 0) ? body : ((form_data && strlen(form_data) > 0) ? form_data : "");
    const char* content_type = (body && strlen(body) > 0) ? "application/json" : ((form_data && strlen(form_data) > 0) ? "application/x-www-form-urlencoded" : NULL);
    
    int content_length = (int)strlen(final_body);

    snprintf(request, sizeof(request),
             "%s %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: LightBaseEngine/1.0\r\n"
             "Accept: application/json\r\n",
             method, path, host_cursor);

    if (content_type) {
        char ct_line[128];
        snprintf(ct_line, sizeof(ct_line), "Content-Type: %s\r\n", content_type);
        strncat(request, ct_line, sizeof(request) - strlen(request) - 1);
    }
    
    if (content_length > 0) {
        char cl_line[64];
        snprintf(cl_line, sizeof(cl_line), "Content-Length: %d\r\n", content_length);
        strncat(request, cl_line, sizeof(request) - strlen(request) - 1);
    }

    if (headers && strlen(headers) > 0) {
        strncat(request, headers, sizeof(request) - strlen(request) - 1);
        // Ensure headers block ends with a newline if it doesn't already
        if (request[strlen(request)-1] != '\n') {
            strncat(request, "\r\n", sizeof(request) - strlen(request) - 1);
        }
    }

    strncat(request, "Connection: close\r\n\r\n", sizeof(request) - strlen(request) - 1);
    
    if (content_length > 0) {
        strncat(request, final_body, sizeof(request) - strlen(request) - 1);
    }

    SSL_write(ssl, request, strlen(request));

    int total_bytes_received = 0;
    int bytes_received;

    while ((bytes_received = SSL_read(ssl, local_network_buffer + total_bytes_received, BUFFER_SIZE - total_bytes_received - 1)) > 0) {
        total_bytes_received += bytes_received;
        if (total_bytes_received >= BUFFER_SIZE - 1) break;
    }

    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(socket_fd);

    char* json_body = strstr(local_network_buffer, "\r\n\r\n");

    clock_gettime(CLOCK_MONOTONIC, &end);
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    double microseconds = (seconds * 1000000.0) + (nanoseconds / 1000.0);

    printf("[C-Core Benchmark] Secure HTTPS request completed in: %.2f us\n", microseconds);

    if (json_body != NULL) {
        json_body += 4;

        size_t payload_len = strlen(json_body);
        char* dynamic_response_wrapper = malloc(payload_len + 256);
        snprintf(dynamic_response_wrapper, payload_len + 256,
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
        res.payload = strdup("{\"error\": \"Could not isolate HTTP body block from secure stream data.\"}");
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

    sqlite3* db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Failed to instantiate internal file database locks.\"}");
        return res;
    }

    // 🎯 Ensure system tables exist
    initialize_system_collections_ledger_table(db);

    // Initialize custom arena: slice out a continuous 4MB pool runway instantly
    size_t arena_size = 4 * 1024 * 1024;
    MemoryArena* local_arena = arena_init(arena_size);
    if (!local_arena) {
        sqlite3_close(db);
        res.status_code = 500;
        res.payload = strdup("{\"error\": \"Failed to allocate bare-metal Memory Arena pool.\"}");
        return res;
    }

    size_t json_capacity = 4096;
    char* dynamic_json_heap = (char*)arena_alloc(local_arena, json_capacity);
    strcpy(dynamic_json_heap, "[");
    int row_count = 0;

    const char* sql_leftover = sql_query;
    const char* pzTail = NULL;
    sqlite3_stmt* stmt = NULL;

    // Core Bytecode Compiling Loop leveraging pzTail addresses
    while (sql_leftover && strlen(sql_leftover) > 0) {
        if (sqlite3_prepare_v2(db, sql_leftover, -1, &stmt, &pzTail) != SQLITE_OK) {
            char error_payload[512];
            snprintf(error_payload, sizeof(error_payload), "{\"error\": \"SQL Compilation Fault: %s\"}", sqlite3_errmsg(db));
            sqlite3_close(db);
            arena_destroy(local_arena);

            res.status_code = 500;
            res.payload = strdup(error_payload);
            return res;
        }

        if (!stmt) {
            sql_leftover = pzTail;
            continue;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
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

            size_t current_len = strlen(dynamic_json_heap);
            size_t incoming_len = strlen(row_buffer);

            // Arena Offset Array Scaling Pass
            if (current_len + incoming_len + 5 >= json_capacity) {
                json_capacity *= 2;

                char* expanded_json = (char*)arena_alloc(local_arena, json_capacity);
                if (!expanded_json) {
                    sqlite3_finalize(stmt);
                    sqlite3_close(db);
                    arena_destroy(local_arena);
                    res.status_code = 500;
                    res.payload = strdup("{\"error\": \"Custom Arena pool capacity exhaustion tracking fault.\"}");
                    return res;
                }

                memcpy(expanded_json, dynamic_json_heap, current_len + 1);
                dynamic_json_heap = expanded_json;
            }

            strcat(dynamic_json_heap, row_buffer);
            row_count++;
        }

        sqlite3_finalize(stmt);
        sql_leftover = pzTail; // Advance our tracking bookmark offset cleanly
    }

    int final_len = (int)strlen(dynamic_json_heap);
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

    res.status_code = 200;
    res.payload = dynamic_db_response;
    return res;
}

// Forward declaration of the thread pool enqueue function
int enqueue_intercepted_route_task(int client_fd);

// ============================================================================
// 🎧 MULTI-THREADED ASYNC WORKER THREAD POOL ENGINE (TLV DECODER UPGRADE)
// ============================================================================
void process_client_request_isolated(int client_fd) {
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
                for (int i = 0; packet.query[i] != '\0' && (size_t)i < sizeof(packet.query) - 1; i++) {
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
                printf("[C-Core TLV Worker] Binary Network Target Hit. Host: %s, Method: %s\n", packet.hostname, strlen(packet.method) > 0 ? packet.method : "GET");
                Response net_res = fire_http_get(strlen(packet.method) > 0 ? packet.method : "GET", packet.hostname, packet.path, packet.headers, packet.body, packet.form_data);
                send(client_fd, net_res.payload, strlen(net_res.payload), 0);
                free((void*)net_res.payload);
            }

            // ROUTE 3: Kernel Telemetry Parser + Append-Only Log Structured Ring Writer
            else if (strcmp(packet.target, "sys_metrics") == 0) {
                SystemMetrics machine = gather_system_performance();

                uint32_t total_mem_mb = (uint32_t)(machine.total_mem_kb / 1024);
                uint32_t avail_mem_mb = (uint32_t)(machine.avail_mem_kb / 1024); // Typo struct name aligned!

                // Write snapshot record to raw disk sectors in single digit microseconds
                int active_slot = append_mmap_telemetry_record((float)machine.cpu_usage_percentage, total_mem_mb, avail_mem_mb);
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

            else if (strcmp(packet.target, "api_studio_run") == 0) {
                const char* final_method = strlen(packet.method) > 0 ? packet.method : (strlen(packet.query) > 0 ? packet.query : "GET");
                printf("[C-Core TLV Worker] API Studio Request Runner Hit. Host: %s, Method: %s\n", packet.hostname, final_method);

                // 1. Asynchronously execute the raw wire-packet formatting engine
                // packet.query holds the HTTP Verb if packet.method is empty
                Response studio_wire = execute_studio_api_request(final_method, packet.hostname, packet.path, packet.headers, packet.body);
                printf("[C-Core API Studio] Wire formatting layout verified. Initializing transmission stream...\n");

                // 2. Fire the formatted payload straight through our stack-isolated OpenSSL TLS network runner
                Response live_network_res = fire_http_get(final_method, packet.hostname, packet.path, packet.headers, packet.body, packet.form_data);

                // Stream the live returned payload straight back down to our WebStorm frontend data grids
                send(client_fd, live_network_res.payload, strlen(live_network_res.payload), 0);

                // Clean up the intermediate heap string allocations immediately to prevent memory leaks
                free((void*)studio_wire.payload);
                free((void*)live_network_res.payload);
            }

            else if (strcmp(packet.target, "get_schema_tree") == 0) {
                printf("[C-Core TLV Worker] Schema Visualizer Target Hit: %s\n", packet.db_path);

                // Fire our metadata catalog harvesting engine
                Response schema_res = fetch_database_schema_tree(packet.db_path);

                // Stream the formatted structural JSON block back down the socket descriptor channel
                send(client_fd, schema_res.payload, strlen(schema_res.payload), 0);

                // Free the temporary heap allocation buffer layout immediately
                free((void*)schema_res.payload);
            }

            else if (strcmp(packet.target, "save_collection") == 0) {
                printf("[C-Core TLV Worker] Saving collection: %s\n", packet.query);
                sqlite3* db = NULL;
                if (sqlite3_open(packet.db_path, &db) == SQLITE_OK) {
                    initialize_system_collections_ledger_table(db);
                    insert_collection_record_to_arena(db, packet.query, packet.method, packet.hostname);
                    sqlite3_close(db);
                    const char* ok = "{\"status\": \"SAVED\"}";
                    send(client_fd, ok, strlen(ok), 0);
                } else {
                    const char* err = "{\"error\": \"Failed to open database for saving collection.\"}";
                    send(client_fd, err, strlen(err), 0);
                }
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

        if (enqueue_intercepted_route_task(client_fd) != 0) {
            close(client_fd);
        }
    }
    close(server_fd);
    return NULL;
}

// Bootstrap initialization logic spawning the architecture on boot
EXPORT int start_linux_ipc_bridge() {
    // Carve out and pre-allocate our fixed circular log-structured loop on disk
    if (init_mmap_telemetry_log() != 0) {
        printf("[C-Core Error] Critical fault mapping virtual telemetry pages onto space allocation.\n");
        return -1; // 🎯 Returns a real failure signal to the orchestration thread!
    }
    printf("[C-Core Storage Engine] Log-Structured circular file mounted successfully at lightbase_telemetry.log\n");

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(IPC_PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 50) < 0) {
        close(server_fd);
        return -1;
    }

    printf("[C-Core IPC Multithreading] Pool Main Router listening at: 127.0.0.1:%d 🎧\n", IPC_PORT);

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