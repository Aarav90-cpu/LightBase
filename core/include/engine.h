#ifndef ENGINE_H
#define ENGINE_H

typedef struct {
    int status_code;
    const char* payload;
} Response;

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

// 1. Ensure this is declared!
EXPORT Response execute_raw_query(const char* query);
EXPORT Response fire_http_get(const char* hostname, const char* path);
EXPORT Response execute_local_db(const char* db_path, const char* sql_query);
EXPORT int start_linux_ipc_bridge();

#endif // ENGINE_H