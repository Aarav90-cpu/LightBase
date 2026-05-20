#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <sched.h>

// ============================================================================
// ⚡ LOCK-FREE THREAD POOL ENGINE
// ============================================================================
// Uses atomic Compare-And-Swap (CAS) operations on a circular SPMC (Single
// Producer, Multiple Consumer) ring buffer. Workers spin-wait with adaptive
// backoff instead of blocking on mutexes, eliminating lock contention under
// high-throughput IPC loads.
//
// The ring buffer uses monotonically increasing head/tail counters with
// modular indexing, avoiding the ABA problem entirely.
// ============================================================================

#define MAX_WORKER_THREADS 8
#define TASK_QUEUE_CAPACITY 256  // Must be power of 2 for fast modular indexing
#define TASK_QUEUE_MASK (TASK_QUEUE_CAPACITY - 1)

// Forward declaration of the handler implemented in engine.c
void process_client_request_isolated(int client_fd);

// --- Task slot with atomic sequence counter for CAS coordination ---
typedef struct {
    _Atomic uint64_t sequence;
    int client_socket_fd;
} TaskSlot;

// --- Lock-Free Ring Buffer Pool ---
typedef struct {
    // Cache-line padded to prevent false sharing between producer and consumers
    _Alignas(64) _Atomic uint64_t enqueue_pos;   // Producer write cursor
    _Alignas(64) _Atomic uint64_t dequeue_pos;   // Consumer read cursor
    _Alignas(64) TaskSlot slots[TASK_QUEUE_CAPACITY]; // Ring buffer slots
    _Alignas(64) atomic_int shutdown_signal;           // Graceful shutdown flag
    pthread_t workers[MAX_WORKER_THREADS];
    _Atomic uint64_t tasks_processed;              // Telemetry counter
    _Atomic uint64_t tasks_enqueued;               // Telemetry counter
    _Atomic uint64_t contention_spins;             // Contention telemetry
} LockFreePool;

static LockFreePool *g_pool = NULL;

// --- Adaptive backoff: yield → nanosleep escalation ---
static inline void backoff_spin(int iteration) {
    if (iteration < 4) {
        // CPU-level pause hint (reduces power and pipeline stalls)
        #if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("pause" ::: "memory");
        #else
        __asm__ volatile("yield" ::: "memory");
        #endif
    } else if (iteration < 16) {
        sched_yield();
    } else {
        struct timespec ts = {0, 1000}; // 1μs sleep
        nanosleep(&ts, NULL);
    }
}

// --- LOCK-FREE ENQUEUE (Producer side, called from acceptor) ---
int enqueue_intercepted_route_task(int client_fd) {
    if (!g_pool) return -1;

    int spin_count = 0;
    while (1) {
        uint64_t pos = atomic_load_explicit(&g_pool->enqueue_pos, memory_order_relaxed);
        TaskSlot *slot = &g_pool->slots[pos & TASK_QUEUE_MASK];
        uint64_t seq = atomic_load_explicit(&slot->sequence, memory_order_acquire);

        int64_t diff = (int64_t)seq - (int64_t)pos;

        if (diff == 0) {
            // Slot is ready for writing — attempt CAS on the enqueue position
            if (atomic_compare_exchange_weak_explicit(
                    &g_pool->enqueue_pos, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                // CAS succeeded — we own this slot, write the task
                slot->client_socket_fd = client_fd;
                // Publish: set sequence to pos + 1 so consumers can see it
                atomic_store_explicit(&slot->sequence, pos + 1, memory_order_release);
                atomic_fetch_add_explicit(&g_pool->tasks_enqueued, 1, memory_order_relaxed);
                return 0;
            }
            // CAS failed — another producer won, retry
        } else if (diff < 0) {
            // Queue is full
            if (spin_count > 32) {
                fprintf(stderr, "[C-Core Pool] Lock-free ring buffer full! Dropping client fd %d.\n", client_fd);
                return -1;
            }
            atomic_fetch_add_explicit(&g_pool->contention_spins, 1, memory_order_relaxed);
            backoff_spin(spin_count++);
        } else {
            // Rare: enqueue_pos was updated by another thread mid-read, reload
            backoff_spin(spin_count++);
        }
    }
}

// --- LOCK-FREE DEQUEUE (Consumer side, called from worker threads) ---
static int try_dequeue(int *out_fd) {
    int spin_count = 0;
    while (1) {
        uint64_t pos = atomic_load_explicit(&g_pool->dequeue_pos, memory_order_relaxed);
        TaskSlot *slot = &g_pool->slots[pos & TASK_QUEUE_MASK];
        uint64_t seq = atomic_load_explicit(&slot->sequence, memory_order_acquire);

        int64_t diff = (int64_t)seq - (int64_t)(pos + 1);

        if (diff == 0) {
            // Slot has data — attempt CAS on the dequeue position
            if (atomic_compare_exchange_weak_explicit(
                    &g_pool->dequeue_pos, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed)) {
                // CAS succeeded — we own this slot, read the task
                *out_fd = slot->client_socket_fd;
                // Recycle: set sequence to pos + CAPACITY so producers can reuse
                atomic_store_explicit(&slot->sequence, pos + TASK_QUEUE_CAPACITY, memory_order_release);
                return 1; // Success
            }
            // CAS failed — another consumer won, retry
        } else if (diff < 0) {
            // Queue is empty
            return 0;
        } else {
            backoff_spin(spin_count++);
        }
    }
}

// --- WORKER THREAD LIFECYCLE (Lock-Free Consumer Loop) ---
static void* lockfree_worker_loop(void *arg) {
    long worker_id = (long)arg;
    printf("[C-Core Worker %ld] Ignition loop online. Ready for task processing.\n", worker_id);

    int idle_spins = 0;

    while (!atomic_load_explicit(&g_pool->shutdown_signal, memory_order_acquire)) {
        int client_fd;
        if (try_dequeue(&client_fd)) {
            idle_spins = 0;
            printf("[C-Core Worker %ld] Processing task on Socket Descriptor: %d\n", worker_id, client_fd);
            process_client_request_isolated(client_fd);
            atomic_fetch_add_explicit(&g_pool->tasks_processed, 1, memory_order_relaxed);
        } else {
            backoff_spin(idle_spins++);
            if (idle_spins > 1000) idle_spins = 16; // Cap backoff at yield level
        }
    }

    printf("[C-Core Worker %ld] Lane loop winding down safely.\n", worker_id);
    return NULL;
}

// --- BOOTSTRAP LOCK-FREE POOL ---
int initialize_c_core_interceptor_pool(void) {
    g_pool = calloc(1, sizeof(LockFreePool));
    if (!g_pool) return -1;

    // Initialize sequence counters for each slot
    for (int i = 0; i < TASK_QUEUE_CAPACITY; i++) {
        atomic_init(&g_pool->slots[i].sequence, (uint64_t)i);
    }

    atomic_init(&g_pool->enqueue_pos, 0);
    atomic_init(&g_pool->dequeue_pos, 0);
    atomic_init(&g_pool->shutdown_signal, 0);
    atomic_init(&g_pool->tasks_processed, 0);
    atomic_init(&g_pool->tasks_enqueued, 0);
    atomic_init(&g_pool->contention_spins, 0);

    // Spawn persistent worker threads
    for (long i = 0; i < MAX_WORKER_THREADS; i++) {
        if (pthread_create(&g_pool->workers[i], NULL, lockfree_worker_loop, (void*)i) != 0) {
            fprintf(stderr, "[C-Core Fault] Failed to create worker thread %ld\n", i);
            return -1;
        }
    }

    printf("[C-Core Pool] Asynchronous Interceptor Grid deployed successfully with %d active threads!\n", MAX_WORKER_THREADS);
    return 0;
}

// --- POOL TELEMETRY (callable from bridge for diagnostics) ---
void get_pool_telemetry(uint64_t *enqueued, uint64_t *processed, uint64_t *spins) {
    if (!g_pool) { *enqueued = *processed = *spins = 0; return; }
    *enqueued = atomic_load_explicit(&g_pool->tasks_enqueued, memory_order_relaxed);
    *processed = atomic_load_explicit(&g_pool->tasks_processed, memory_order_relaxed);
    *spins = atomic_load_explicit(&g_pool->contention_spins, memory_order_relaxed);
}