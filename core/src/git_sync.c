#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <git2.h>

// 🎯 FIX VERIFICATION: Ensure there is NO 'static' keyword here! 
// This must be a globally visible symbol so lb_cli.o can bind to it.
char* compile_native_git_branch_status(const char* repo_path) {
    git_repository *repo = NULL;
    git_reference *head_ref = NULL;
    char *git_status_buffer = malloc(8192);
    if (!git_status_buffer) return NULL;
    memset(git_status_buffer, 0, 8192);

    git_libgit2_init();

    if (git_repository_open(&repo, repo_path) != 0) {
        snprintf(git_status_buffer, 8192, "❌ [C-Core Error] Path '%s' is not a valid Git repository.\n", repo_path);
        git_libgit2_shutdown();
        return git_status_buffer;
    }

    strcat(git_status_buffer, "⛓️ LightBase Native Git Sync Status\n");
    strcat(git_status_buffer, "===================================\n");

    if (git_repository_head(&head_ref, repo) == 0) {
        const char *branch_name = NULL;
        if (git_branch_name(&branch_name, head_ref) == 0) {
            char branch_info[256];
            snprintf(branch_info, sizeof(branch_info), "🔹 Active Branch: **%s**\n", branch_name);
            strcat(git_status_buffer, branch_info);
        }
        git_reference_free(head_ref);
    } else {
        strcat(git_status_buffer, "⚠️ HEAD reference unmapped.\n");
    }

    git_repository_free(repo);
    git_libgit2_shutdown();

    return git_status_buffer;
}