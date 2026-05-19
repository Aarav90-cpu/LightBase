#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- FORWARD DECLARATIONS OF WRAPPED ENGINE CORE SYMBOLS ---
char* compile_native_markdown_schema_spec(const char* db_path);
char* execute_native_quickjs_assert_suite(const char* test_script, const char* response_json_str);
char* compile_native_git_branch_status(const char* repo_path);
char* execute_local_ai_inference_stream(const char* prompt_context);

void display_cli_usage_help(void) {
    printf("⚡ LightBase Headless C-Binary Executor (`lb-cli`) ⚡\n");
    printf("Usage: ./lb-cli [options]\n\n");
    printf("Options:\n");
    printf("  --db <path> --autogen-docs       Generate Markdown spec specification from DB catalog\n");
    printf("  --test <script_path> <resp_path> Run QuickJS assertion test suite on local JSON payload\n");
    printf("  --help                           Display this structural cockpit help manifest\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        display_cli_usage_help();
        return 1;
    }

    // --- EXECUTION BRANCH A: MARKDOWN AUTO-GENERATION ENGINE DUMP ---
    if (strcmp(argv[1], "--db") == 0 && argc >= 4 && strcmp(argv[3], "--autogen-docs") == 0) {
        const char* db_target_path = argv[2];
        printf("[lb-cli] Initializing bare-metal introspection sweep on target database: %s\n", db_target_path);

        // Direct invocation of Target Phi string builder arrays!
        char* compiled_markdown_document = compile_native_markdown_schema_spec(db_target_path);

        if (compiled_markdown_document) {
            printf("\n%s\n", compiled_markdown_document);
            free(compiled_markdown_document); // Safely reclaim memory buffer space
        } else {
            fprintf(stderr, "[lb-cli Fault] Core engine failed to compile markdown document allocation.\n");
            return -1;
        }
        return 0;
    }

    // --- EXECUTION BRANCH B: LOCAL QUICKJS ASSERTION RUNNER ---
    if (strcmp(argv[1], "--test") == 0 && argc >= 4) {
        const char* script_path = argv[2];
        const char* response_json_path = argv[3];
        printf("[lb-cli] Booting virtual QuickJS arena. Loading files:\n - Script: %s\n - Response: %s\n", script_path, response_json_path);

        // Read test script file
        FILE* script_file = fopen(script_path, "r");
        if (!script_file) {
            fprintf(stderr, "[lb-cli Fault] Cannot open test script file: %s\n", script_path);
            return -1;
        }
        fseek(script_file, 0, SEEK_END);
        long script_size = ftell(script_file);
        fseek(script_file, 0, SEEK_SET);
        char* script_content = malloc(script_size + 1);
        if (!script_content) { fclose(script_file); return -1; }
        fread(script_content, 1, script_size, script_file);
        script_content[script_size] = '\0';
        fclose(script_file);

        // Read response JSON file
        FILE* json_file = fopen(response_json_path, "r");
        if (!json_file) {
            fprintf(stderr, "[lb-cli Fault] Cannot open response JSON file: %s\n", response_json_path);
            free(script_content);
            return -1;
        }
        fseek(json_file, 0, SEEK_END);
        long json_size = ftell(json_file);
        fseek(json_file, 0, SEEK_SET);
        char* json_content = malloc(json_size + 1);
        if (!json_content) { fclose(json_file); free(script_content); return -1; }
        fread(json_content, 1, json_size, json_file);
        json_content[json_size] = '\0';
        fclose(json_file);

        // Fire the QuickJS assertion engine
        char* test_output = execute_native_quickjs_assert_suite(script_content, json_content);
        if (test_output) {
            printf("\n%s\n", test_output);
            free(test_output);
        } else {
            fprintf(stderr, "[lb-cli Fault] QuickJS assertion engine returned NULL.\n");
        }

        free(script_content);
        free(json_content);
        return 0;
    }

    if (strcmp(argv[1], "--help") == 0) {
        display_cli_usage_help();
        return 0;
    }

    if (strcmp(argv[1], "--git-status") == 0 && argc >= 3) {
        const char* target_repo_path = argv[2];
        printf("[lb-cli] Initializing libgit2 structural sweep on tracking repo path: %s\n", target_repo_path);

        char* git_report = compile_native_git_branch_status(target_repo_path);
        if (git_report) {
            printf("\n%s\n", git_report);
            free(git_report);
        } else {
            fprintf(stderr, "[lb-cli Fault] Failed to introspect target git graph.\n");
            return -1;
        }
        return 0;
    }
    
    if (strcmp(argv[1], "--ai") == 0 && argc >= 3) {
        const char* user_prompt = argv[2];
        printf("[lb-cli] Initializing local socket stream channel down to llama.cpp runway...\n");

        // Call the native AI core module directly
        char* ai_generation = execute_local_ai_inference_stream(user_prompt);

        if (ai_generation) {
            printf("\n🤖 [Local AI Agent Generation]:\n\n%s\n", ai_generation);
            free(ai_generation);
        } else {
            fprintf(stderr, "[lb-cli Fault] Core engine dropped context tokens or failed inference.\n");
            return -1;
        }
        return 0;
    }

    fprintf(stderr, "[lb-cli Error] Unknown command argument combinations passed.\n");
    display_cli_usage_help();
    return 1;
}



