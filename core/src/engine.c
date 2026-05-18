#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <dlfcn.h>
#include <time.h>
#include <sys/un.h>
#include <pthread.h>
#define SOCKET_PATH "/tmp/lightbase.sock"

#define BUFFER_SIZE 8192
char response_buffer[BUFFER_SIZE];

// Global dynamic pointer layout variables for SQLite
char* dynamic_db_buffer = NULL;
size_t dynamic_db_capacity = 0;
int db_row_counter = 0;

// === 1. LOCAL MOCK ENGINE ===
EXPORT Response execute_raw_query(const char* query) {
    printf("[C-Core] Executing query: %s\n", query);
    Response res;
    res.status_code = 200;
    res.payload = "{\"status\": \"success\", \"data\": [1, 2, 3]}";
    return res;
}

// === 2. OUTBOUND SOCKET NETWORKING ENGINE (WITH TIMERS) ===
EXPORT Response fire_http_get(const char* hostname, const char* path) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("[C-Core] Preparing raw TCP socket for: %s%s\n", hostname, path);
    Response res = {0, NULL};
    struct addrinfo hints, *server_info;
    int socket_fd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(hostname, "80", &hints, &server_info) != 0) {
        res.status_code = 500;
        res.payload = "{\"error\": \"Hostname resolution failed\"}";
        return res;
    }

    socket_fd = socket(server_info->ai_family, server_info->ai_socktype, server_info->ai_protocol);
    if (socket_fd < 0) {
        freeaddrinfo(server_info);
        res.status_code = 500;
        res.payload = "{\"error\": \"Failed to create socket\"}";
        return res;
    }

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (connect(socket_fd, server_info->ai_addr, server_info->ai_addrlen) < 0) {
        close(socket_fd);
        freeaddrinfo(server_info);
        res.status_code = 500;
        res.payload = "{\"error\": \"Connection to server failed\"}";
        return res;
    }
    freeaddrinfo(server_info);

    char request[512];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: LightBaseEngine/1.0\r\n"
             "Accept: application/json\r\n"
             "Connection: close\r\n\r\n",
             path, hostname);

    send(socket_fd, request, strlen(request), 0);

    memset(response_buffer, 0, BUFFER_SIZE);
    int total_bytes_received = 0;
    int bytes_received;

    while ((bytes_received = recv(socket_fd, response_buffer + total_bytes_received, BUFFER_SIZE - total_bytes_received - 1, 0)) > 0) {
        total_bytes_received += bytes_received;
        if (total_bytes_received >= BUFFER_SIZE - 1) break;
    }

    close(socket_fd);

    char* json_body = strstr(response_buffer, "\r\n\r\n");

    clock_gettime(CLOCK_MONOTONIC, &end);
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    double microseconds = (seconds * 1000000.0) + (nanoseconds / 1000.0);

    printf("[C-Core Benchmark] HTTP request completed in: %.2f us\n", microseconds);

    if (json_body != NULL) {
        json_body += 4;

        char raw_network_json[6144];
        strncpy(raw_network_json, json_body, sizeof(raw_network_json) - 1);

        memset(response_buffer, 0, sizeof(response_buffer));
        snprintf(response_buffer, sizeof(response_buffer),
                 "{"
                 "\"network_payload\": %s,"
                 "\"c_core_duration_us\": %.2f"
                 "}",
                 raw_network_json, microseconds);

        res.status_code = 200;
        res.payload = response_buffer;
    } else {
        res.status_code = 400;
        res.payload = "{\"error\": \"Could not parse HTTP response body\"}";
    }

    return res;
}

// === 3. LOCAL DATABASING ENGINE (WITH DYNAMIC MEMORY HEAPS) ===
static int db_row_callback(void* NotUsed, int argc, char** argv, char** azColName) {
    char row_json[512] = {0};
    int offset = 0;

    offset += snprintf(row_json + offset, sizeof(row_json) - offset, "{");
    for (int i = 0; i < argc; i++) {
        offset += snprintf(row_json + offset, sizeof(row_json) - offset,
                           "\"%s\": \"%s\"", azColName[i], argv[i] ? argv[i] : "NULL");
        if (i < argc - 1) offset += snprintf(row_json + offset, sizeof(row_json) - offset, ", ");
    }
    snprintf(row_json + offset, sizeof(row_json) - offset, "}");

    size_t current_len = strlen(dynamic_db_buffer);
    size_t needed_space = current_len + strlen(row_json) + 5;

    if (needed_space >= dynamic_db_capacity) {
        dynamic_db_capacity *= 2;
        char* new_ptr = realloc(dynamic_db_buffer, dynamic_db_capacity);
        if (!new_ptr) return 1;
        dynamic_db_buffer = new_ptr;
    }

    if (db_row_counter > 0) strcat(dynamic_db_buffer, ", ");
    strcat(dynamic_db_buffer, row_json);
    db_row_counter++;
    return 0;
}

EXPORT Response execute_local_db(const char* db_path, const char* sql_query) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("[C-Core] Dynamic opening database: %s\n", db_path);
    Response res = {0, NULL};

    dynamic_db_capacity = 1024;
    dynamic_db_buffer = malloc(dynamic_db_capacity);
    if (!dynamic_db_buffer) {
        res.status_code = 500;
        res.payload = "{\"error\": \"Failed to allocate tracking memory pools.\"}";
        return res;
    }
    memset(dynamic_db_buffer, 0, dynamic_db_capacity);
    db_row_counter = 0;
    strcpy(dynamic_db_buffer, "[");

    void* handle = dlopen("libsqlite3.so.0", RTLD_LAZY);
    if (!handle) handle = dlopen("libsqlite3.so", RTLD_LAZY);

    if (!handle) {
        free(dynamic_db_buffer);
        res.status_code = 404;
        res.payload = "{\"error\": \"libsqlite3.so not found on host system.\"}";
        return res;
    }

    int (*sqlite_open)(const char*, void**) = dlsym(handle, "sqlite3_open");
    int (*sqlite_exec)(void*, const char*, int (*)(void*, int, char**, char**), void*, char**) = dlsym(handle, "sqlite3_exec");
    int (*sqlite_close)(void*) = dlsym(handle, "sqlite3_close");

    if (!sqlite_open || !sqlite_exec || !sqlite_close) {
        free(dynamic_db_buffer);
        dlclose(handle);
        res.status_code = 500;
        res.payload = "{\"error\": \"Failed to resolve SQLite system symbols.\"}";
        return res;
    }

    void* db = NULL;
    if (sqlite_open(db_path, &db) != 0) {
        free(dynamic_db_buffer);
        dlclose(handle);
        res.status_code = 400;
        res.payload = "{\"error\": \"Failed to open database file.\"}";
        return res;
    }

    char* err_msg = NULL;
    int rc = sqlite_exec(db, sql_query, db_row_callback, NULL, &err_msg);

    strcat(dynamic_db_buffer, "]");

    sqlite_close(db);
    dlclose(handle);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long seconds = end.tv_sec - start.tv_sec;
    long nanoseconds = end.tv_nsec - start.tv_nsec;
    double microseconds = (seconds * 1000000.0) + (nanoseconds / 1000.0);

    printf("[C-Core Benchmark] DB execution completed in: %.2f us\n", microseconds);

    if (rc != 0) {
        memset(dynamic_db_buffer, 0, dynamic_db_capacity);
        snprintf(dynamic_db_buffer, dynamic_db_capacity, "{\"error\": \"SQL Error: %s\"}", err_msg);
        res.status_code = 400;
        res.payload = dynamic_db_buffer;
    } else {
        char* master_payload = malloc(dynamic_db_capacity + 256);
        if (master_payload) {
            snprintf(master_payload, dynamic_db_capacity + 256,
                     "{\"rows\": %s, \"c_core_duration_us\": %.2f}",
                     dynamic_db_buffer, microseconds);
            free(dynamic_db_buffer);
            dynamic_db_buffer = master_payload;
            res.status_code = 200;
            res.payload = dynamic_db_buffer;
        } else {
            res.status_code = 500;
            res.payload = "{\"error\": \"Out of memory allocation errors.\"}";
        }
    }

    return res;
}

void* ipc_worker_thread_loop(void* arg) {
    int server_fd = *(int*)arg;
    free(arg);

    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);

    printf("[C-Core IPC Worker] Background router online. Waiting for instructions... ⚡\n");

    while (1) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        char inbound_packet[2048] = {0};
        ssize_t bytes_read = recv(client_fd, inbound_packet, sizeof(inbound_packet) - 1, 0);

        if (bytes_read > 0) {
            // ROUTE 1: Database Actions
            if (strstr(inbound_packet, "\"target\": \"local_db\"") != NULL) {
                printf("[C-Core IPC Router] Directing request to Database Engine...\n");
                char extracted_db_path[256] = {0};
                char extracted_query[1024] = {0};

                char* path_start = strstr(inbound_packet, "\"db_path\": \"");
                if (path_start) {
                    path_start += 11;
                    char* path_end = strchr(path_start, '"');
                    if (path_end) strncpy(extracted_db_path, path_start, path_end - path_start);
                }

                char* query_start = strstr(inbound_packet, "\"query\": \"");
                if (query_start) {
                    query_start += 10;
                    char* query_end = strchr(query_start, '"');
                    if (query_end) strncpy(extracted_query, query_start, query_end - query_start);
                }

                if (strlen(extracted_db_path) == 0) strcpy(extracted_db_path, "test_lightbase.db");
                if (strlen(extracted_query) == 0) strcpy(extracted_query, "SELECT * FROM users;");

                char sanitized_query[1024] = {0};
                int j = 0;
                for (int i = 0; extracted_query[i] != '\0' && i < sizeof(extracted_query) - 1; i++) {
                    if (extracted_query[i] == '\\' && extracted_query[i + 1] == 'n') {
                        sanitized_query[j++] = ' ';
                        i++;
                    } else if (extracted_query[i] == '\r' || extracted_query[i] == '\n' || extracted_query[i] == '\t') {
                        sanitized_query[j++] = ' ';
                    } else {
                        sanitized_query[j++] = extracted_query[i];
                    }
                }
                sanitized_query[j] = '\0';

                Response db_res = execute_local_db(extracted_db_path, sanitized_query);
                send(client_fd, db_res.payload, strlen(db_res.payload), 0);
            }

            // ROUTE 2: NEW Outbound Network Action over IPC Sockets!
            else if (strstr(inbound_packet, "\"target\": \"network\"") != NULL) {
                printf("[C-Core IPC Router] Directing request to Socket Networking Engine...\n");

                char extracted_host[256] = {0};
                char extracted_path[256] = {0};

                // Extract host variable characters
                char* host_start = strstr(inbound_packet, "\"hostname\": \"");
                if (host_start) {
                    host_start += 13;
                    char* host_end = strchr(host_start, '"');
                    if (host_end) strncpy(extracted_host, host_start, host_end - host_start);
                }

                // Extract path variable characters
                char* path_start = strstr(inbound_packet, "\"path\": \"");
                if (path_start) {
                    path_start += 9;
                    char* path_end = strchr(path_start, '"');
                    if (path_end) strncpy(extracted_path, path_start, path_end - path_start);
                }

                if (strlen(extracted_host) == 0) strcpy(extracted_host, "jsonplaceholder.typicode.com");
                if (strlen(extracted_path) == 0) strcpy(extracted_path, "/posts/1");

                printf("[C-Core IPC Network Targets] Host: %s | Path: %s\n", extracted_host, extracted_path);

                // Call our manual TCP socket handler function
                Response net_res = fire_http_get(extracted_host, extracted_path);

                // Flush the response data right back up the socket stream file
                send(client_fd, net_res.payload, strlen(net_res.payload), 0);
            }
            else {
                const char* ack = "{\"ipc_status\": \"ACK\", \"msg\": \"Handshake route verified\"}";
                send(client_fd, ack, strlen(ack), 0);
            }
        }
        close(client_fd);
    }
    close(server_fd);
    return NULL;
}
EXPORT int start_linux_ipc_bridge() {
    int server_fd;
    struct sockaddr_un server_addr;

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    unlink(SOCKET_PATH);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 5) < 0) {
        close(server_fd);
        return -1;
    }

    printf("[C-Core IPC Engine] Channel listening at: %s\n", SOCKET_PATH);

    // Allocate an isolated memory address for the file descriptor to prevent thread race conditions
    int* server_fd_alloc = malloc(sizeof(int));
    *server_fd_alloc = server_fd;

    // Spin up the background POSIX worker thread
    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, ipc_worker_thread_loop, server_fd_alloc) != 0) {
        printf("[C-Core IPC Error] Failed to delegate worker thread loops.\n");
        free(server_fd_alloc);
        close(server_fd);
        return -1;
    }

    // Detach the thread layout so it runs completely independently in the background
    pthread_detach(thread_id);

    return 0; // Return immediately back up to Python while the worker thread hums along!
}