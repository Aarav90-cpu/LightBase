#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_WORKER_THREADS 8
#define TASK_QUEUE_CAPACITY 128

// Forward declaration of the handler implemented in engine.c
void process_client_request_isolated(int client_fd);

// --- STRUCTURAL TASK ENTRY UNIT ---
typedef struct {
    int client_socket_fd;
} LightBaseTask;

// --- ARENA THREAD POOL CONTROL cabin ---
typedef struct {
    pthread_t workers[MAX_WORKER_THREADS];
    LightBaseTask queue[TASK_QUEUE_CAPACITY];
    int head;
    int tail;
    int size;
    pthread_mutex_t lock;
    pthread_cond_t cond_var;
    int shutdown_signal;
} LightBaseThreadPool;

static LightBaseThreadPool *global_pool = NULL;

// --- WORKER THREAD LANE LIFECYCLE LOOP ---
void* interceptor_worker_lane_loop(void *arg) {
    long worker_id = (long)arg;
    printf("[C-Core Worker %ld] Ignition loop online. Ready for task processing.\n", worker_id);

    while (1) {
        pthread_mutex_lock(&(global_pool->lock));

        // Sleep lock-free until a task populates the queue buffer matrix
        while (global_pool->size == 0 && !global_pool->shutdown_signal) {
            pthread_cond_wait(&(global_pool->cond_var), &(global_pool->lock));
        }

        if (global_pool->shutdown_signal && global_pool->size == 0) {
            pthread_mutex_unlock(&(global_pool->lock));
            break;
        }

        // Dequeue an intercepted routing task frame
        LightBaseTask task = global_pool->queue[global_pool->head];
        global_pool->head = (global_pool->head + 1) % TASK_QUEUE_CAPACITY;
        global_pool->size--;

        pthread_mutex_unlock(&(global_pool->lock));

        // 🎯 REAL PROCESSING: Dispatch to our unified request handler
        printf("[C-Core Worker %ld] Processing task on Socket Descriptor: %d\n", worker_id, task.client_socket_fd);
        process_client_request_isolated(task.client_socket_fd);
    }

    printf("[C-Core Worker %ld] Lane loop winding down safely.\n", worker_id);
    return NULL;
}

// --- BOOTSTRAP THREAD POOL IGNITION ---
int initialize_c_core_interceptor_pool(void) {
    global_pool = malloc(sizeof(LightBaseThreadPool));
    if (!global_pool) return -1;

    global_pool->head = 0;
    global_pool->tail = 0;
    global_pool->size = 0;
    global_pool->shutdown_signal = 0;

    pthread_mutex_init(&(global_pool->lock), NULL);
    pthread_cond_init(&(global_pool->cond_var), NULL);

    // Spawn the persistent background POSIX thread slots
    for (long i = 0; i < MAX_WORKER_THREADS; i++) {
        if (pthread_create(&(global_pool->workers[i]), NULL, interceptor_worker_lane_loop, (void*)i) != 0) {
            fprintf(stderr, "[C-Core Fault] Failed to allocate thread worker ring index %ld\n", i);
            return -1;
        }
    }

    printf("[C-Core Pool] Asynchronous Interceptor Grid deployed successfully with %d active threads!\n", MAX_WORKER_THREADS);
    return 0;
}

// --- SUBMIT INTERCEPTED ROUTE TASK ---
int enqueue_intercepted_route_task(int client_fd) {
    if (!global_pool) return -1;
    
    pthread_mutex_lock(&(global_pool->lock));

    if (global_pool->size == TASK_QUEUE_CAPACITY) {
        pthread_mutex_unlock(&(global_pool->lock));
        fprintf(stderr, "[C-Core Overload] Task ring buffer capacity reached! Dropping packet.\n");
        return -1;
    }

    // Lock task properties straight into the circular buffer layout track
    global_pool->queue[global_pool->tail].client_socket_fd = client_fd;

    global_pool->tail = (global_pool->tail + 1) % TASK_QUEUE_CAPACITY;
    global_pool->size++;

    // Signal and wake up a sleeping worker thread lane instantly!
    pthread_cond_signal(&(global_pool->cond_var));
    pthread_mutex_unlock(&(global_pool->lock));

    return 0;
}