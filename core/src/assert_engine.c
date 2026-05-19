#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <quickjs/quickjs.h>
// Ensure the quickjs header files are inside your include paths!

char* execute_native_quickjs_assert_suite(const char* test_script, const char* response_json_str) {
    JSRuntime *rt;
    JSContext *ctx;
    JSValue global_obj, response_obj, js_log;
    char* assertion_output_log = malloc(16384);
    if (!assertion_output_log) return NULL;
    memset(assertion_output_log, 0, 16384);

    // 1. Boot up an isolated virtual engine runtime context channel frame
    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);

    // 2. Parse raw response text fields into structural context tokens
    response_obj = JS_ParseJSON(ctx, response_json_str, strlen(response_json_str), "<input>");
    global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "response", response_obj);

    // 3. Inject standard test suite macro string parameters
    const char* assert_macro =
        "let passedCount = 0; let failedCount = 0; let log = '';"
        "function assert(condition, message) {"
        "   if (condition) { passedCount++; log += '✅ PASS: ' + message + '\\n'; }"
        "   else { failedCount++; log += '❌ FAIL: ' + message + '\\n'; }"
        "}";
    JS_Eval(ctx, assert_macro, strlen(assert_macro), "<macro>", JS_EVAL_TYPE_GLOBAL);

    // 4. Run the user-input test scripts inside the virtual context arena space
    JS_Eval(ctx, test_script, strlen(test_script), "<user_test>", JS_EVAL_TYPE_GLOBAL);

    // 5. Extract structural results data from our virtual execution loop layers
    const char* format_str = "`🏁 Test Results: ${passedCount} passed, ${failedCount} failed\\n${log}`";
    js_log = JS_Eval(ctx, format_str, strlen(format_str), "<format>", JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(js_log)) {
        JSValue except = JS_GetException(ctx);
        const char* err_str = JS_ToCString(ctx, except);
        snprintf(assertion_output_log, 16384, "💥 [QuickJS Exception] %s", err_str ? err_str : "Unknown error");
        if (err_str) JS_FreeCString(ctx, err_str);
        JS_FreeValue(ctx, except);
    } else {
        // 🎯 CRITICAL FIX: Swap out the non-existent symbol 'JS_ToCStringLen2' for the official 'JS_ToCStringLen' function mapping block signature!
        size_t log_string_length = 0;
        const char* final_log_ptr = JS_ToCStringLen(ctx, &log_string_length, js_log);

        if (final_log_ptr) {
            strncpy(assertion_output_log, final_log_ptr, 16383);
            JS_FreeCString(ctx, final_log_ptr);
        } else {
            snprintf(assertion_output_log, 16384, "💥 [C-Core Error] Assertion script parsing fault or evaluation dropped inside runtime context channels.");
        }
    }

    // 🛡️ CLEAN UNWIND: Safe register release sequence parameters
    JS_FreeValue(ctx, js_log);
    JS_FreeValue(ctx, global_obj);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    return assertion_output_log;
}
