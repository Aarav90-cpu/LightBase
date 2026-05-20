#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <git2.h>
#include <time.h>
#include "engine.h"

// ============================================================================
// 🧠 REACTIVE GIT-STATE INTERCEPTOR ENGINE
// ============================================================================
// Uses Linux inotify to watch .git/HEAD for branch switches, then fires
// libgit2 to extract the full state diff. The watcher runs on a dedicated
// POSIX thread and writes state changes to a shared volatile struct that
// the Python bridge polls via IPC.
// ============================================================================

#define GIT_EVENT_BUFFER_SIZE 4096
#define MAX_CHANGED_FILES 128

// --- Shared volatile state struct visible to the bridge ---
typedef struct {
    volatile int      dirty;                     // 1 = new event available
    char              branch[128];               // Current active branch name
    char              prev_branch[128];          // Previous branch before switch
    char              event_type[32];            // "branch_switch", "merge", "pull", "commit"
    uint64_t          timestamp;                 // Epoch time of event
    int               changed_file_count;        // Number of workspace files that changed
    char              changed_files[MAX_CHANGED_FILES][256]; // File paths
    int               schema_changed;            // 1 if any .db/.sql file changed
    int               config_changed;            // 1 if any workspace .json changed
    int               plugin_changed;            // 1 if any .py plugin changed
    char              head_commit_id[48];        // Short SHA of HEAD
    pthread_mutex_t   lock;
} GitReactiveState;

static GitReactiveState g_git_state = {0};
static volatile int g_watcher_running = 0;
static char g_repo_path[512] = {0};

// --- Read current branch name via libgit2 ---
static int read_current_branch(const char* repo_path, char* out_branch, size_t len, char* out_sha, size_t sha_len) {
    git_repository *repo = NULL;
    git_reference *head_ref = NULL;
    int rc = -1;

    git_libgit2_init();

    if (git_repository_open(&repo, repo_path) != 0) {
        git_libgit2_shutdown();
        return -1;
    }

    // Get HEAD commit SHA
    git_object *head_obj = NULL;
    if (git_revparse_single(&head_obj, repo, "HEAD") == 0) {
        const git_oid *oid = git_object_id(head_obj);
        char hex[GIT_OID_SHA1_HEXSIZE + 1] = {0};
        git_oid_tostr(hex, sizeof(hex), oid);
        snprintf(out_sha, sha_len, "%.8s", hex);
        git_object_free(head_obj);
    }

    if (git_repository_head(&head_ref, repo) == 0) {
        const char *branch_name = NULL;
        if (git_branch_name(&branch_name, head_ref) == 0) {
            strncpy(out_branch, branch_name, len - 1);
            rc = 0;
        }
        git_reference_free(head_ref);
    }

    git_repository_free(repo);
    git_libgit2_shutdown();
    return rc;
}

// --- Detect changed files by reading git status ---
static int detect_changed_files(const char* repo_path, GitReactiveState* state) {
    git_repository *repo = NULL;
    git_status_list *status_list = NULL;
    int count = 0;

    git_libgit2_init();

    if (git_repository_open(&repo, repo_path) != 0) {
        git_libgit2_shutdown();
        return 0;
    }

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    if (git_status_list_new(&status_list, repo, &opts) == 0) {
        size_t n = git_status_list_entrycount(status_list);

        state->schema_changed = 0;
        state->config_changed = 0;
        state->plugin_changed = 0;

        for (size_t i = 0; i < n && count < MAX_CHANGED_FILES; i++) {
            const git_status_entry *entry = git_status_byindex(status_list, i);
            const char *path = NULL;

            if (entry->index_to_workdir && entry->index_to_workdir->new_file.path)
                path = entry->index_to_workdir->new_file.path;
            else if (entry->head_to_index && entry->head_to_index->new_file.path)
                path = entry->head_to_index->new_file.path;

            if (path) {
                strncpy(state->changed_files[count], path, 255);
                count++;

                // Classify the changed file
                size_t plen = strlen(path);
                if (plen > 3 && (strcmp(path + plen - 3, ".db") == 0 || strcmp(path + plen - 4, ".sql") == 0))
                    state->schema_changed = 1;
                if (strstr(path, "workspace/") && plen > 5 && strcmp(path + plen - 5, ".json") == 0)
                    state->config_changed = 1;
                if (strstr(path, "plugins/") && plen > 3 && strcmp(path + plen - 3, ".py") == 0)
                    state->plugin_changed = 1;
            }
        }
        git_status_list_free(status_list);
    }

    git_repository_free(repo);
    git_libgit2_shutdown();
    return count;
}

// --- Background inotify watcher thread ---
static void* git_inotify_watcher_thread(void* arg) {
    (void)arg;
    printf("[C-Core Git Watcher] 🧠 Reactive interceptor thread online.\n");

    // Build path to .git/HEAD and .git/refs/heads/
    char head_path[600], refs_path[600];
    snprintf(head_path, sizeof(head_path), "%s/.git", g_repo_path);
    snprintf(refs_path, sizeof(refs_path), "%s/.git/refs/heads", g_repo_path);

    // Read initial branch state
    char initial_branch[128] = {0}, initial_sha[48] = {0};
    read_current_branch(g_repo_path, initial_branch, sizeof(initial_branch), initial_sha, sizeof(initial_sha));

    pthread_mutex_lock(&g_git_state.lock);
    strncpy(g_git_state.branch, initial_branch, sizeof(g_git_state.branch) - 1);
    strncpy(g_git_state.head_commit_id, initial_sha, sizeof(g_git_state.head_commit_id) - 1);
    strcpy(g_git_state.event_type, "init");
    g_git_state.timestamp = (uint64_t)time(NULL);
    g_git_state.dirty = 1;
    pthread_mutex_unlock(&g_git_state.lock);

    printf("[C-Core Git Watcher] Initial branch: %s (%s)\n", initial_branch, initial_sha);

    int inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        perror("[C-Core Git Watcher] inotify_init failed");
        return NULL;
    }

    // Watch .git directory for HEAD changes (branch switches)
    int wd_git = inotify_add_watch(inotify_fd, head_path, IN_MODIFY | IN_CREATE | IN_MOVED_TO);
    if (wd_git < 0) {
        perror("[C-Core Git Watcher] Failed to watch .git/");
        close(inotify_fd);
        return NULL;
    }

    // Also watch refs/heads for new branch commits
    int wd_refs = -1;
    if (access(refs_path, F_OK) == 0) {
        wd_refs = inotify_add_watch(inotify_fd, refs_path, IN_MODIFY | IN_CREATE | IN_MOVED_TO);
    }

    printf("[C-Core Git Watcher] inotify watches armed on .git/ and refs/heads/\n");

    char buf[GIT_EVENT_BUFFER_SIZE];

    while (g_watcher_running) {
        // Use a timeout so we can check shutdown flag
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(inotify_fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int sel = select(inotify_fd + 1, &read_fds, NULL, NULL, &tv);
        if (sel <= 0) continue; // Timeout or error, loop back

        ssize_t len = read(inotify_fd, buf, sizeof(buf));
        if (len <= 0) continue;

        // Debounce: Git operations trigger multiple inotify events rapidly.
        // Wait 200ms for things to settle before reading the new state.
        usleep(200000);

        // Read the new branch state
        char new_branch[128] = {0}, new_sha[48] = {0};
        if (read_current_branch(g_repo_path, new_branch, sizeof(new_branch), new_sha, sizeof(new_sha)) != 0) {
            continue; // Transient state, skip
        }

        pthread_mutex_lock(&g_git_state.lock);

        // Determine event type
        if (strcmp(g_git_state.branch, new_branch) != 0) {
            // Branch switch detected!
            strncpy(g_git_state.prev_branch, g_git_state.branch, sizeof(g_git_state.prev_branch) - 1);
            strncpy(g_git_state.branch, new_branch, sizeof(g_git_state.branch) - 1);
            strcpy(g_git_state.event_type, "branch_switch");
            printf("[C-Core Git Watcher] ⚡ Branch switch detected: %s → %s\n", g_git_state.prev_branch, new_branch);
        } else if (strcmp(g_git_state.head_commit_id, new_sha) != 0) {
            // Same branch but new commit (could be commit, merge, pull, rebase)
            strcpy(g_git_state.event_type, "head_update");
            printf("[C-Core Git Watcher] 🔄 HEAD update on %s: %s → %s\n", new_branch, g_git_state.head_commit_id, new_sha);
        } else {
            pthread_mutex_unlock(&g_git_state.lock);
            continue; // No meaningful change
        }

        strncpy(g_git_state.head_commit_id, new_sha, sizeof(g_git_state.head_commit_id) - 1);
        g_git_state.timestamp = (uint64_t)time(NULL);

        // Detect changed workspace files
        g_git_state.changed_file_count = detect_changed_files(g_repo_path, &g_git_state);

        g_git_state.dirty = 1;
        pthread_mutex_unlock(&g_git_state.lock);
    }

    if (wd_refs >= 0) inotify_rm_watch(inotify_fd, wd_refs);
    inotify_rm_watch(inotify_fd, wd_git);
    close(inotify_fd);
    printf("[C-Core Git Watcher] Watcher thread terminated cleanly.\n");
    return NULL;
}

// ============================================================================
// EXPORTED FUNCTIONS (called from Python bridge via ctypes)
// ============================================================================

// Start the background watcher thread
EXPORT int start_git_reactive_watcher(const char* repo_path) {
    if (g_watcher_running) return 0; // Already running

    strncpy(g_repo_path, repo_path, sizeof(g_repo_path) - 1);
    pthread_mutex_init(&g_git_state.lock, NULL);
    g_watcher_running = 1;

    pthread_t watcher_tid;
    if (pthread_create(&watcher_tid, NULL, git_inotify_watcher_thread, NULL) != 0) {
        g_watcher_running = 0;
        return -1;
    }
    pthread_detach(watcher_tid);
    return 0;
}

// Poll the latest git state (non-blocking, returns JSON string)
EXPORT char* poll_git_reactive_state(void) {
    char* json_buf = malloc(32768);
    if (!json_buf) return NULL;

    pthread_mutex_lock(&g_git_state.lock);

    // Build changed files JSON array
    char files_json[16384] = "[";
    for (int i = 0; i < g_git_state.changed_file_count && i < MAX_CHANGED_FILES; i++) {
        char entry[300];
        snprintf(entry, sizeof(entry), "%s\"%s\"", (i > 0 ? "," : ""), g_git_state.changed_files[i]);
        strncat(files_json, entry, sizeof(files_json) - strlen(files_json) - 1);
    }
    strcat(files_json, "]");

    snprintf(json_buf, 32768,
        "{"
        "\"dirty\": %d,"
        "\"branch\": \"%s\","
        "\"prev_branch\": \"%s\","
        "\"event_type\": \"%s\","
        "\"timestamp\": %lu,"
        "\"head_sha\": \"%s\","
        "\"changed_file_count\": %d,"
        "\"changed_files\": %s,"
        "\"schema_changed\": %d,"
        "\"config_changed\": %d,"
        "\"plugin_changed\": %d"
        "}",
        g_git_state.dirty,
        g_git_state.branch,
        g_git_state.prev_branch,
        g_git_state.event_type,
        (unsigned long)g_git_state.timestamp,
        g_git_state.head_commit_id,
        g_git_state.changed_file_count,
        files_json,
        g_git_state.schema_changed,
        g_git_state.config_changed,
        g_git_state.plugin_changed);

    // Clear dirty flag after read
    g_git_state.dirty = 0;

    pthread_mutex_unlock(&g_git_state.lock);
    return json_buf;
}

// Acknowledge and clear the event (called after bridge processes it)
EXPORT void ack_git_reactive_event(void) {
    pthread_mutex_lock(&g_git_state.lock);
    g_git_state.dirty = 0;
    g_git_state.changed_file_count = 0;
    memset(g_git_state.changed_files, 0, sizeof(g_git_state.changed_files));
    g_git_state.schema_changed = 0;
    g_git_state.config_changed = 0;
    g_git_state.plugin_changed = 0;
    strcpy(g_git_state.event_type, "idle");
    pthread_mutex_unlock(&g_git_state.lock);
}
