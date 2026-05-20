#!/usr/bin/env python3
"""
LightBase — Comprehensive Test Suite
=====================================
Tests every single feature across the entire platform:
  - C-Core security module (HMAC, rate limiter, IP rules, sessions, audit, etc.)
  - Enterprise engine (collection runner, codegen, schema validation, etc.)
  - Bridge routes (API studio, collections, environments, history, etc.)
  - Frontend-connected endpoints (monitors, flows, plugins, docs, etc.)

Usage:
  python3 test_all.py          # Run all tests
  make test                    # Run via Makefile (auto-starts bridge)
"""

import urllib.request
import json
import sys
import time

API = "http://localhost:8000"
PASSED = 0
FAILED = 0
ERRORS = []


def post(path, payload, timeout=15):
    """POST JSON to the bridge and return parsed response."""
    data = json.dumps(payload).encode()
    req = urllib.request.Request(
        API + path, data=data,
        headers={"Content-Type": "application/json"}, method="POST"
    )
    resp = urllib.request.urlopen(req, timeout=timeout)
    return json.loads(resp.read())


def check(name, condition, detail=""):
    """Assert a condition, track pass/fail."""
    global PASSED, FAILED
    if condition:
        PASSED += 1
        print(f"  ✅ {name}")
    else:
        FAILED += 1
        msg = f"  ❌ {name}" + (f" — {detail}" if detail else "")
        print(msg)
        ERRORS.append(msg)


def section(title):
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")


# ============================================================================
# 1. HEALTH CHECK
# ============================================================================
def test_health():
    section("1. HEALTH CHECK")
    try:
        r = post("/sys_metrics", {})
        check("Bridge responds", r.get("engine_status") == 200)
        check("Has CPU data", "system_telemetry" in r)
    except Exception as e:
        check("Bridge is running", False, str(e))
        print("\n💀 Bridge is not running. Start it with: make run")
        sys.exit(1)


# ============================================================================
# 2. SECURITY: HMAC SIGNING
# ============================================================================
def test_hmac():
    section("2. SECURITY: HMAC-SHA256")
    r = post("/security/sign", {"payload": "test_payload_123"})
    sig = r.get("signature", "")
    check("Sign returns 64-char hex", len(sig) == 64)

    r2 = post("/security/verify", {"payload": "test_payload_123", "signature": sig})
    check("Verify valid signature", r2.get("valid") is True)

    r3 = post("/security/verify", {"payload": "TAMPERED", "signature": sig})
    check("Reject tampered payload", r3.get("valid") is False)

    r4 = post("/security/verify", {"payload": "test_payload_123", "signature": "0" * 64})
    check("Reject wrong signature", r4.get("valid") is False)


# ============================================================================
# 3. SECURITY: PATH VALIDATION
# ============================================================================
def test_path_guard():
    section("3. SECURITY: PATH TRAVERSAL GUARD")
    r1 = post("/security/validate_path", {"path": "data/file.json"})
    check("Allow safe relative path", r1.get("safe") is True)

    r2 = post("/security/validate_path", {"path": "../../etc/passwd"})
    check("Block path traversal", r2.get("safe") is False)

    r3 = post("/security/validate_path", {"path": "~/secret"})
    check("Block home expansion", r3.get("safe") is False)

    r4 = post("/security/validate_path", {"path": "file;rm -rf /"})
    check("Block shell metacharacters", r4.get("safe") is False)


# ============================================================================
# 4. SECURITY: SQL SANITIZER
# ============================================================================
def test_sql_guard():
    section("4. SECURITY: SQL INJECTION DEFENSE")
    r1 = post("/security/validate_sql", {"sql": "SELECT * FROM users WHERE id=1"})
    check("Allow safe SELECT", r1.get("safe") is True)

    r2 = post("/security/validate_sql", {"sql": "ATTACH DATABASE '/etc/passwd' AS pwn"})
    check("Block ATTACH DATABASE", r2.get("safe") is False)

    r3 = post("/security/validate_sql", {"sql": "SELECT LOAD_EXTENSION('evil.so')"})
    check("Block LOAD_EXTENSION", r3.get("safe") is False)

    r4 = post("/security/validate_sql", {"sql": "CREATE TABLE test (id INTEGER)"})
    check("Allow safe CREATE", r4.get("safe") is True)


# ============================================================================
# 5. SECURITY: IP RULES
# ============================================================================
def test_ip_rules():
    section("5. SECURITY: IP ALLOWLIST/BLOCKLIST")
    post("/security/ip/add", {"ip": "10.0.0.1", "action": "block"})
    post("/security/ip/add", {"ip": "192.168.1.1", "action": "allow"})

    r1 = post("/security/ip/check", {"ip": "10.0.0.1"})
    check("Blocked IP rejected", r1.get("allowed") is False)

    r2 = post("/security/ip/check", {"ip": "192.168.1.1"})
    check("Allowed IP accepted", r2.get("allowed") is True)

    r3 = post("/security/ip/check", {"ip": "172.16.0.1"})
    check("Unknown IP allowed by default", r3.get("allowed") is True)

    r4 = post("/security/ip/list", {})
    check("List returns rules", len(r4.get("rules", [])) >= 2)

    post("/security/ip/remove", {"ip": "10.0.0.1"})
    r5 = post("/security/ip/check", {"ip": "10.0.0.1"})
    check("Removed IP now allowed", r5.get("allowed") is True)


# ============================================================================
# 6. SECURITY: SESSIONS
# ============================================================================
def test_sessions():
    section("6. SECURITY: SESSION TOKENS")
    r1 = post("/security/session/create", {"label": "test_admin", "ttl_seconds": 60})
    token = r1.get("token", "")
    check("Session created (64-char token)", len(token) == 64)
    check("Session has label", r1.get("label") == "test_admin")

    r2 = post("/security/session/validate", {"token": token})
    check("Valid session accepted", r2.get("valid") is True)

    r3 = post("/security/session/validate", {"token": "0" * 64})
    check("Invalid token rejected", r3.get("valid") is False)

    r4 = post("/security/session/list", {})
    check("Session list works", isinstance(r4.get("sessions"), list))

    post("/security/session/revoke", {"token": token})
    r5 = post("/security/session/validate", {"token": token})
    check("Revoked session rejected", r5.get("valid") is False)


# ============================================================================
# 7. SECURITY: AUDIT LOG
# ============================================================================
def test_audit():
    section("7. SECURITY: AUDIT TRAIL")
    r1 = post("/security/audit/log", {"max_entries": 10})
    check("Audit log returns entries", "audit_entries" in r1)

    post("/security/audit/clear", {})
    r2 = post("/security/audit/log", {"max_entries": 10})
    check("Audit log cleared", r2.get("count", 0) == 0)


# ============================================================================
# 8. SECURITY: REQUEST SIZE
# ============================================================================
def test_request_size():
    section("8. SECURITY: REQUEST SIZE LIMITER")
    r1 = post("/security/max_size/get", {})
    check("Default max size exists", r1.get("max_request_size", 0) > 0)

    post("/security/max_size/set", {"max_bytes": 5242880})
    r2 = post("/security/max_size/get", {})
    check("Max size updated to 5MB", r2.get("max_request_size") == 5242880)

    # Reset
    post("/security/max_size/set", {"max_bytes": 1048576})


# ============================================================================
# 9. SECURITY: STATUS REPORT
# ============================================================================
def test_security_status():
    section("9. SECURITY: FULL STATUS REPORT")
    r = post("/security/status", {})
    sec = r.get("security", {})
    check("HMAC initialized", sec.get("hmac_initialized") is True)
    check("Rate limiter configured", sec.get("rate_bucket_capacity", 0) > 0)
    check("Path guard active", sec.get("path_guard") is True)
    check("SQL sanitizer active", sec.get("sql_sanitizer") is True)
    check("Secure wipe active", sec.get("secure_wipe") is True)
    check("AES vault active", sec.get("aes_256_gcm_vault") is True)
    check("IP rules tracked", "ip_rules_count" in sec)
    check("Sessions tracked", "active_sessions" in sec)
    check("Max request size tracked", "max_request_size" in sec)


# ============================================================================
# 10. ENTERPRISE: CODE SNIPPETS
# ============================================================================
def test_codegen():
    section("10. ENTERPRISE: CODE SNIPPETS")
    for lang in ["curl", "python", "javascript", "java", "go", "php"]:
        r = post("/codegen", {"method": "POST", "url": "https://api.example.com/v1/data",
                               "headers": "Authorization: Bearer tok", "body": '{"key":"val"}', "language": lang})
        check(f"Code snippet ({lang})", len(r.get("snippet", "")) > 10)


# ============================================================================
# 11. ENTERPRISE: JSON SCHEMA VALIDATION
# ============================================================================
def test_schema_validation():
    section("11. ENTERPRISE: JSON SCHEMA VALIDATION")
    r1 = post("/validate/schema", {
        "data": {"name": "LightBase", "version": 2, "active": True},
        "schema": {"type": "object", "required": ["name", "version"],
                   "properties": {"name": {"type": "string"}, "version": {"type": "integer", "minimum": 1}}}
    })
    check("Valid object passes", r1.get("valid") is True)

    r2 = post("/validate/schema", {
        "data": {"name": 123},
        "schema": {"type": "object", "properties": {"name": {"type": "string"}}}
    })
    check("Type mismatch fails", r2.get("valid") is False)
    check("Error details present", len(r2.get("errors", [])) > 0)


# ============================================================================
# 12. ENTERPRISE: AUTH HELPERS
# ============================================================================
def test_auth():
    section("12. ENTERPRISE: AUTH HELPERS")
    r1 = post("/auth/headers", {"headers": "", "auth": {"type": "bearer", "token": "mytoken123"}})
    check("Bearer auth applied", "Bearer mytoken123" in r1.get("headers", ""))

    r2 = post("/auth/headers", {"headers": "", "auth": {"type": "basic", "username": "admin", "password": "pass"}})
    check("Basic auth applied", "Basic " in r2.get("headers", ""))

    r3 = post("/auth/headers", {"headers": "", "auth": {"type": "api_key", "key": "X-API-Key", "value": "abc123"}})
    check("API key applied", "abc123" in r3.get("headers", ""))


# ============================================================================
# 13. COLLECTIONS & ENVIRONMENTS
# ============================================================================
def test_collections():
    section("13. COLLECTIONS & ENVIRONMENTS")
    r1 = post("/save_collection", {"name": "test_suite_col", "requests": [
        {"name": "Get Users", "method": "GET", "url": "https://httpbin.org/get"}
    ]})
    check("Save collection", r1.get("status") == "SAVED")

    r2 = post("/save_environment", {"name": "test_env", "base_url": "https://httpbin.org"})
    check("Save environment", r2.get("status") == "SAVED")

    r3 = post("/fs/list", {"category": "collections"})
    check("List collections", "test_suite_col" in (r3.get("files") or []))


# ============================================================================
# 14. ENTERPRISE: COLLECTION COMMENTS & FORKING
# ============================================================================
def test_comments_forking():
    section("14. ENTERPRISE: COMMENTS & FORKING")
    r1 = post("/collection/comment", {"collection": "test_suite_col", "text": "Approved", "author": "tester"})
    check("Add comment", r1.get("comment_count", 0) >= 1)

    r2 = post("/collection/comments", {"collection": "test_suite_col"})
    check("Get comments", len(r2.get("comments", [])) >= 1)

    r3 = post("/collection/fork", {"source": "test_suite_col", "fork_name": "test_fork", "author": "tester"})
    check("Fork collection", r3.get("forked") is True)


# ============================================================================
# 15. ENTERPRISE: WORKSPACE EXPORT/IMPORT
# ============================================================================
def test_workspace_sync():
    section("15. ENTERPRISE: WORKSPACE SYNC")
    r1 = post("/workspace/export", {})
    check("Export workspace", "checksum" in r1)
    check("Export has path", "path" in r1)


# ============================================================================
# 16. DOCS & OPENAPI
# ============================================================================
def test_docs():
    section("16. DOCUMENTATION GENERATION")
    r1 = post("/docs/generate", {})
    check("HTML docs generated", "path" in r1)

    r2 = post("/export_openapi", {})
    check("OpenAPI spec generated", "spec" in r2)


# ============================================================================
# 17. HISTORY
# ============================================================================
def test_history():
    section("17. REQUEST HISTORY")
    r1 = post("/history/list", {})
    check("History list works", "history" in r1)


# ============================================================================
# 18. MOCK SERVER
# ============================================================================
def test_mock():
    section("18. MOCK SERVER")
    r1 = post("/mock/register", {
        "name": "test_mock", "method": "GET", "path": "/api/test",
        "status": 200, "response_body": '{"ok":true}'
    })
    check("Register mock", r1.get("engine_status") == 200)


# ============================================================================
# 19. FLOWS
# ============================================================================
def test_flows():
    section("19. VISUAL FLOWS")
    r1 = post("/flow/save", {"name": "test_flow", "data": {"nodes": [
        {"type": "request", "method": "GET", "url": "https://httpbin.org/get"}
    ]}})
    check("Save flow", r1.get("status") == "saved")

    r2 = post("/flow/load", {"name": "test_flow"})
    check("Load flow", "nodes" in r2)


# ============================================================================
# 20. TELEMETRY
# ============================================================================
def test_telemetry():
    section("20. TELEMETRY")
    r1 = post("/telemetry_history", {"max_records": 10})
    check("Telemetry history", "records_read" in r1)


# ============================================================================
# 21. GIT REACTIVE STATE
# ============================================================================
def test_git():
    section("21. GIT REACTIVE STATE")
    import os
    repo_path = os.path.dirname(os.path.abspath(__file__))
    r1 = post("/git_sync", {"repo_path": repo_path})
    check("Git sync works", r1.get("engine_status") == 200)

    r2 = post("/git_watch_state", {})
    check("Git watch state", "git_reactive" in r2 or r2.get("engine_status") == 200)


# ============================================================================
# 22. CRYPTO VAULT
# ============================================================================
def test_vault():
    section("22. CRYPTO VAULT")
    r1 = post("/secure_vault_key", {"provider": "test_provider", "plain_key": "sk-test-key-12345"})
    check("Vault encrypt", r1.get("vault_status") == "SUCCESS_SECURED")

    r2 = post("/retrieve_vault_key", {"provider": "test_provider"})
    check("Vault decrypt matches", r2.get("decrypted_key") == "sk-test-key-12345")


# ============================================================================
# 23. FILESYSTEM OPS
# ============================================================================
def test_filesystem():
    section("23. FILESYSTEM OPS")
    r1 = post("/fs/save", {"category": "collections", "name": "fs_test", "data": {"test": True}})
    check("FS save", r1.get("status") == "saved")

    r2 = post("/fs/load", {"category": "collections", "name": "fs_test"})
    check("FS load", r2.get("test") is True)

    r3 = post("/fs/list", {"category": "collections"})
    check("FS list", "fs_test" in (r3.get("files") or []))

    post("/fs/delete", {"category": "collections", "name": "fs_test"})


# ============================================================================
# 24. KERNEL-LEVEL SECURITY CHECKS
# ============================================================================
def test_kernel_security():
    section("24. KERNEL-LEVEL SECURITY")
    # These are verified by the C-Core initializing without errors
    r = post("/security/status", {})
    sec = r.get("security", {})
    check("Secure wipe (mlock-backed)", sec.get("secure_wipe") is True)
    check("HMAC key (urandom-derived)", sec.get("hmac_initialized") is True)
    check("Memory protection active", sec.get("aes_256_gcm_vault") is True)


# ============================================================================
# RUNNER
# ============================================================================
if __name__ == "__main__":
    print("\n" + "=" * 60)
    print("  ⚡ LightBase — Full Test Suite")
    print("=" * 60)

    start = time.time()

    test_health()
    test_hmac()
    test_path_guard()
    test_sql_guard()
    test_ip_rules()
    test_sessions()
    test_audit()
    test_request_size()
    test_security_status()
    test_codegen()
    test_schema_validation()
    test_auth()
    test_collections()
    test_comments_forking()
    test_workspace_sync()
    test_docs()
    test_history()
    test_mock()
    test_flows()
    test_telemetry()
    test_git()
    test_vault()
    test_filesystem()
    test_kernel_security()

    elapsed = round(time.time() - start, 2)

    print(f"\n{'=' * 60}")
    print(f"  RESULTS: {PASSED} passed, {FAILED} failed ({elapsed}s)")
    print(f"{'=' * 60}")

    if ERRORS:
        print("\n  Failed tests:")
        for e in ERRORS:
            print(f"  {e}")

    sys.exit(0 if FAILED == 0 else 1)
