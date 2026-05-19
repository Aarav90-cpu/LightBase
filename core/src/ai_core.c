#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h> // 🎯 FIX 1: Enforce explicit tracking of POSIX signals!

#define LLAMA_SOCKET_PATH "/tmp/llama.sock"

// --- NATIVE OFFLINE AI INTELLIGENCE INTERCEPTOR CORE ---
char* execute_local_ai_inference_stream(const char* prompt_context) {
    int socket_fd;
    struct sockaddr_un server_addr;
    char *response_buffer = malloc(32768); // Allocate massive 32KB generation block runway
    if (!response_buffer) return NULL;
    memset(response_buffer, 0, 32768);

    // 🎯 FIX 2: Enforce correct SOCK_STREAM type initialization parameters!
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        snprintf(response_buffer, 32768, "💥 [C-Core AI Error] Failed to spin up raw socket file descriptor allocation lane.");
        return response_buffer;
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, LLAMA_SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

    // Connect directly onto our background headless llama.cpp server instance socket pointer
    if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_un)) < 0) {
        snprintf(response_buffer, 32768, "❌ [C-Core AI Error] Headless llama.cpp server offline at %s. Please initialize your local engine runway!", LLAMA_SOCKET_PATH);
        close(socket_fd);
        return response_buffer;
    }

    printf("[C-Core AI] Encapsulating local system context tracking parameters...\n");

    // 🎯 FIX 3: Defensive Shield! We swap write() for send() with MSG_NOSIGNAL
    // to cleanly catch pipe breaks without triggering kernel crashes!
    if (send(socket_fd, prompt_context, strlen(prompt_context), MSG_NOSIGNAL) < 0) {
        snprintf(response_buffer, 32768, "💥 [C-Core AI Error] Stream transmission failure dropping context blocks.");
        close(socket_fd);
        return response_buffer;
    }

    // Ingest generation tokens sequentially down the wire
    size_t total_bytes_read = 0;
    ssize_t batch_bytes = 0;
    char stream_chunk[2048];

    while ((batch_bytes = read(socket_fd, stream_chunk, sizeof(stream_chunk) - 1)) > 0) {
        if (total_bytes_read + batch_bytes >= 32767) break; // Hard upper boundary safety guard
        memcpy(response_buffer + total_bytes_read, stream_chunk, batch_bytes);
        total_bytes_read += batch_bytes;
    }

    close(socket_fd);
    return response_buffer;
}