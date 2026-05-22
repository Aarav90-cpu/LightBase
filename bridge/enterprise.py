"""
LightBase Enterprise Engine
============================
Enterprise-grade features matching Postman's usage by companies like Reliance Jio:

1. Collection Runner   — Run entire collections with variable chaining & test assertions
2. CI/CD Reporter      — JUnit XML + HTML report generation for Jenkins/GitHub Actions
3. JSON Schema Validator — Validate API responses against JSON Schema drafts
4. Workspace Sync      — Export/import workspaces in Postman-compatible format
5. API Documentation   — Generate full HTML documentation portals from collections
6. Request Chaining    — Extract response values via JSONPath into environment variables
"""

import json, os, time, re, uuid, socket, traceback, hashlib
from datetime import datetime
from xml.etree.ElementTree import Element, SubElement, tostring
from xml.dom.minidom import parseString

import os
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKSPACE = os.path.join(BASE_DIR, "workspace")
for d in ["reports", "exports", "docs/html"]:
    os.makedirs(f"{WORKSPACE}/{d}", exist_ok=True)


# ============================================================================
# 1. VARIABLE INTERPOLATION & JSONPATH EXTRACTION
# ============================================================================

# Built-in dynamic variables (like Postman's {{$timestamp}}, {{$guid}}, etc.)
def resolve_dynamic_vars(text, env_vars=None):
    """Replace {{variable}} placeholders with values from env or builtins."""
    if not text or not isinstance(text, str):
        return text

    builtins = {
        "$timestamp": str(int(time.time())),
        "$isoTimestamp": datetime.utcnow().isoformat() + "Z",
        "$guid": str(uuid.uuid4()),
        "$randomInt": str(__import__('random').randint(0, 99999)),
        "$randomEmail": f"user{__import__('random').randint(100,999)}@test.io",
    }

    def replacer(match):
        key = match.group(1).strip()
        if key in builtins:
            return builtins[key]
        if env_vars and key in env_vars:
            return str(env_vars[key])
        return match.group(0)  # Leave unresolved

    return re.sub(r'\{\{(.+?)\}\}', replacer, text)


def jsonpath_extract(data, path):
    """Simple JSONPath-like extractor: 'response.data.items[0].id'"""
    try:
        parts = re.split(r'\.|\[|\]', path)
        parts = [p for p in parts if p]
        current = data
        for part in parts:
            if part.isdigit():
                current = current[int(part)]
            else:
                current = current[part]
        return current
    except (KeyError, IndexError, TypeError):
        return None


# ============================================================================
# 2. COLLECTION RUNNER ENGINE
# ============================================================================

def execute_single_request(req, env_vars, lightbase_lib, encode_tlv_fn):
    """Execute a single API request with variable interpolation, return result dict."""
    method = resolve_dynamic_vars(req.get("method", "GET"), env_vars)
    url = resolve_dynamic_vars(req.get("url", ""), env_vars)
    headers_raw = req.get("headers", "")
    if isinstance(headers_raw, list):
        headers_raw = "\r\n".join(headers_raw)
    headers_str = resolve_dynamic_vars(headers_raw, env_vars)
    body = resolve_dynamic_vars(req.get("body", ""), env_vars)

    # Parse host/path from URL
    clean = url.replace("https://", "").replace("http://", "")
    si = clean.find("/")
    host = clean[:si] if si != -1 else clean
    path = clean[si:] if si != -1 else "/"

    start_time = time.time()
    response_body = ""
    status_code = 0
    error = None

    try:
        # Use IPC to C-Core for the actual network request
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(30)
        sock.connect(("127.0.0.1", 8001))
        tlv = (encode_tlv_fn(0x01, "network") + encode_tlv_fn(0x04, host) +
               encode_tlv_fn(0x05, path) + encode_tlv_fn(0x06, headers_str) +
               encode_tlv_fn(0x07, method) + encode_tlv_fn(0x08, body) +
               encode_tlv_fn(0x09, ""))
        sock.sendall(tlv)

        chunks = []
        while True:
            chunk = sock.recv(262144)
            if not chunk:
                break
            chunks.append(chunk)
        sock.close()
        response_body = b"".join(chunks).decode("utf-8", errors="replace")
        status_code = 200
    except Exception as e:
        error = str(e)
        status_code = 0

    elapsed_ms = round((time.time() - start_time) * 1000, 2)

    # Parse response JSON if possible
    response_json = None
    try:
        response_json = json.loads(response_body)
    except Exception:
        pass

    return {
        "name": req.get("name", url),
        "method": method,
        "url": url,
        "status_code": status_code,
        "response_time_ms": elapsed_ms,
        "response_body": response_body[:10000],
        "response_json": response_json,
        "response_size": len(response_body),
        "error": error,
    }


def run_test_script(test_script, response_json, response_body, status_code, lightbase_lib):
    """Run a QuickJS test script against the response, return results."""
    if not test_script or not lightbase_lib:
        return {"passed": 0, "failed": 0, "log": ""}

    resp_obj = json.dumps({
        "status": status_code,
        "body": response_json if response_json else response_body[:5000],
        "text": response_body[:5000],
    })

    result = lightbase_lib.execute_native_quickjs_assert_suite(
        test_script.encode(), resp_obj.encode()
    )

    log = result.decode() if result else "Error running tests"
    passed = log.count("✅ PASS")
    failed = log.count("❌ FAIL")

    return {"passed": passed, "failed": failed, "log": log}


def run_collection(collection_data, env_vars, iterations, lightbase_lib, encode_tlv_fn):
    """
    Run an entire collection sequentially with:
    - Variable interpolation from environment
    - Test script execution per request
    - Variable extraction (chaining) between requests
    - Multiple iteration support
    """
    requests = collection_data.get("requests", [])
    if isinstance(collection_data, dict) and "url" in collection_data and not requests:
        requests = [collection_data]

    all_iterations = []
    total_passed = 0
    total_failed = 0
    total_requests = 0
    start_time = time.time()

    # Merge environment
    run_env = dict(env_vars) if env_vars else {}

    for iteration in range(max(1, iterations)):
        iteration_results = []

        for req in requests:
            # Execute request with current env
            result = execute_single_request(req, run_env, lightbase_lib, encode_tlv_fn)
            total_requests += 1

            # Variable extraction (chaining)
            extractions = req.get("extract", {})
            for var_name, json_path in extractions.items():
                if result["response_json"]:
                    extracted = jsonpath_extract(result["response_json"], json_path)
                    if extracted is not None:
                        run_env[var_name] = extracted

            # Run test script
            test_script = req.get("test_script", req.get("tests", ""))
            test_result = run_test_script(
                test_script, result["response_json"],
                result["response_body"], result["status_code"],
                lightbase_lib
            )
            result["test_results"] = test_result
            total_passed += test_result["passed"]
            total_failed += test_result["failed"]

            iteration_results.append(result)

        all_iterations.append({
            "iteration": iteration + 1,
            "results": iteration_results,
        })

    total_time = round((time.time() - start_time) * 1000, 2)

    return {
        "collection_name": collection_data.get("name", "Unnamed"),
        "timestamp": datetime.utcnow().isoformat() + "Z",
        "total_requests": total_requests,
        "total_passed": total_passed,
        "total_failed": total_failed,
        "total_time_ms": total_time,
        "iterations": all_iterations,
        "final_environment": run_env,
        "success": total_failed == 0,
    }


# ============================================================================
# 3. CI/CD REPORT GENERATORS
# ============================================================================

def generate_junit_xml(run_result):
    """Generate JUnit XML report from collection run results (for Jenkins/GH Actions)."""
    testsuites = Element("testsuites")
    testsuites.set("name", run_result.get("collection_name", "LightBase"))
    testsuites.set("tests", str(run_result["total_passed"] + run_result["total_failed"]))
    testsuites.set("failures", str(run_result["total_failed"]))
    testsuites.set("time", str(run_result["total_time_ms"] / 1000))

    for iter_data in run_result.get("iterations", []):
        testsuite = SubElement(testsuites, "testsuite")
        testsuite.set("name", f"Iteration {iter_data['iteration']}")

        for result in iter_data.get("results", []):
            testcase = SubElement(testsuite, "testcase")
            testcase.set("name", result.get("name", ""))
            testcase.set("classname", f"{result['method']} {result['url']}")
            testcase.set("time", str(result["response_time_ms"] / 1000))

            tr = result.get("test_results", {})
            if tr.get("failed", 0) > 0:
                failure = SubElement(testcase, "failure")
                failure.set("message", f"{tr['failed']} assertions failed")
                failure.text = tr.get("log", "")

            if result.get("error"):
                error_el = SubElement(testcase, "error")
                error_el.set("message", result["error"])

    raw_xml = tostring(testsuites, encoding="unicode")
    pretty = parseString(raw_xml).toprettyxml(indent="  ")
    return pretty


def generate_html_report(run_result):
    """Generate a styled HTML test report."""
    col_name = run_result.get("collection_name", "LightBase")
    passed = run_result["total_passed"]
    failed = run_result["total_failed"]
    total = passed + failed
    pass_rate = round((passed / max(total, 1)) * 100, 1)
    status_color = "#00e676" if failed == 0 else "#ff5252"

    rows = ""
    for iter_data in run_result.get("iterations", []):
        for r in iter_data.get("results", []):
            tr = r.get("test_results", {})
            row_color = "#1b5e20" if tr.get("failed", 0) == 0 else "#b71c1c"
            rows += f"""<tr style="border-bottom:1px solid #333">
                <td style="padding:8px">{r['method']}</td>
                <td style="padding:8px;color:#64b5f6">{r['url'][:60]}</td>
                <td style="padding:8px">{r['response_time_ms']}ms</td>
                <td style="padding:8px;color:{row_color}">{tr.get('passed',0)}✅ {tr.get('failed',0)}❌</td>
                <td style="padding:8px;color:#888;font-size:11px">{r.get('error','') or 'OK'}</td>
            </tr>"""

    html = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>{col_name} — Test Report</title>
<style>
body{{background:#0d1117;color:#e6edf3;font-family:'Segoe UI',sans-serif;padding:40px;margin:0}}
.card{{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:24px;margin-bottom:20px}}
h1{{color:#58a6ff;margin:0 0 8px}} h2{{color:#8b949e;font-weight:400;font-size:14px;margin:0 0 20px}}
.stat{{display:inline-block;padding:12px 24px;border-radius:8px;margin:0 8px 8px 0;font-weight:700;font-size:18px}}
table{{width:100%;border-collapse:collapse;font-size:13px}}
th{{text-align:left;padding:10px 8px;color:#8b949e;border-bottom:2px solid #30363d}}
.badge{{padding:4px 12px;border-radius:20px;font-weight:700;font-size:13px}}
</style></head><body>
<div class="card">
  <h1>🧪 {col_name}</h1>
  <h2>Test Report — {run_result['timestamp']}</h2>
  <div class="stat" style="background:#0d2818;color:#00e676">✅ {passed} Passed</div>
  <div class="stat" style="background:#2d0a0a;color:#ff5252">❌ {failed} Failed</div>
  <div class="stat" style="background:#1a1a2e;color:#64b5f6">⏱ {run_result['total_time_ms']}ms</div>
  <div class="stat" style="background:#1a1a2e;color:#bb86fc">📊 {pass_rate}% Pass Rate</div>
</div>
<div class="card">
  <table><thead><tr><th>Method</th><th>URL</th><th>Time</th><th>Assertions</th><th>Status</th></tr></thead>
  <tbody>{rows}</tbody></table>
</div>
<div style="text-align:center;color:#484f58;padding:20px;font-size:12px">
  Generated by LightBase Enterprise Engine • {run_result['total_requests']} requests executed
</div></body></html>"""
    return html


# ============================================================================
# 4. JSON SCHEMA VALIDATOR
# ============================================================================

def validate_json_schema(data, schema):
    """Lightweight JSON Schema validator (subset of Draft-07)."""
    errors = []

    def validate(value, sch, path="$"):
        if sch is None:
            return

        # Type check
        expected_type = sch.get("type")
        if expected_type:
            type_map = {"string": str, "number": (int, float), "integer": int,
                        "boolean": bool, "array": list, "object": dict, "null": type(None)}
            if expected_type in type_map and not isinstance(value, type_map[expected_type]):
                errors.append(f"{path}: expected {expected_type}, got {type(value).__name__}")
                return

        # String validations
        if isinstance(value, str):
            if "minLength" in sch and len(value) < sch["minLength"]:
                errors.append(f"{path}: string too short (min {sch['minLength']})")
            if "maxLength" in sch and len(value) > sch["maxLength"]:
                errors.append(f"{path}: string too long (max {sch['maxLength']})")
            if "pattern" in sch and not re.search(sch["pattern"], value):
                errors.append(f"{path}: does not match pattern '{sch['pattern']}'")
            if "enum" in sch and value not in sch["enum"]:
                errors.append(f"{path}: '{value}' not in enum {sch['enum']}")

        # Number validations
        if isinstance(value, (int, float)):
            if "minimum" in sch and value < sch["minimum"]:
                errors.append(f"{path}: {value} < minimum {sch['minimum']}")
            if "maximum" in sch and value > sch["maximum"]:
                errors.append(f"{path}: {value} > maximum {sch['maximum']}")

        # Array validations
        if isinstance(value, list):
            if "minItems" in sch and len(value) < sch["minItems"]:
                errors.append(f"{path}: array too short (min {sch['minItems']})")
            if "maxItems" in sch and len(value) > sch["maxItems"]:
                errors.append(f"{path}: array too long (max {sch['maxItems']})")
            if "items" in sch:
                for i, item in enumerate(value):
                    validate(item, sch["items"], f"{path}[{i}]")

        # Object validations
        if isinstance(value, dict):
            required = sch.get("required", [])
            for req_field in required:
                if req_field not in value:
                    errors.append(f"{path}: missing required field '{req_field}'")

            properties = sch.get("properties", {})
            for prop_name, prop_schema in properties.items():
                if prop_name in value:
                    validate(value[prop_name], prop_schema, f"{path}.{prop_name}")

    validate(data, schema)
    return {"valid": len(errors) == 0, "errors": errors}


# ============================================================================
# 5. WORKSPACE EXPORT/IMPORT (Postman-Compatible Format)
# ============================================================================

def export_workspace(workspace_path):
    """Export entire workspace as a single shareable JSON package."""
    package = {
        "format": "lightbase_workspace_v1",
        "exported_at": datetime.utcnow().isoformat() + "Z",
        "collections": {},
        "environments": {},
        "flows": {},
        "plugins": {},
    }

    for category in ["collections", "environments", "flows"]:
        cat_dir = f"{workspace_path}/{category}"
        if os.path.isdir(cat_dir):
            for f in os.listdir(cat_dir):
                if f.endswith(".json"):
                    name = os.path.splitext(f)[0]
                    with open(f"{cat_dir}/{f}") as fh:
                        try:
                            package[category][name] = json.load(fh)
                        except Exception:
                            pass

    # Include plugins
    plugin_dir = f"{workspace_path}/plugins"
    if os.path.isdir(plugin_dir):
        for f in os.listdir(plugin_dir):
            if f.endswith(".py"):
                with open(f"{plugin_dir}/{f}") as fh:
                    package["plugins"][f] = fh.read()

    # Generate checksum for integrity
    raw = json.dumps(package, sort_keys=True)
    package["checksum"] = hashlib.sha256(raw.encode()).hexdigest()

    return package


def import_workspace(package, workspace_path, merge=True):
    """Import a workspace package, optionally merging with existing data."""
    imported = {"collections": 0, "environments": 0, "flows": 0, "plugins": 0}

    for category in ["collections", "environments", "flows"]:
        items = package.get(category, {})
        cat_dir = f"{workspace_path}/{category}"
        os.makedirs(cat_dir, exist_ok=True)
        for name, data in items.items():
            target = f"{cat_dir}/{name}.json"
            if merge or not os.path.exists(target):
                with open(target, 'w') as f:
                    json.dump(data, f, indent=2)
                imported[category] += 1

    # Import plugins
    plugins = package.get("plugins", {})
    plugin_dir = f"{workspace_path}/plugins"
    os.makedirs(plugin_dir, exist_ok=True)
    for fname, code in plugins.items():
        target = f"{plugin_dir}/{fname}"
        if merge or not os.path.exists(target):
            with open(target, 'w') as f:
                f.write(code)
            imported["plugins"] += 1

    return imported


# ============================================================================
# 6. HTML API DOCUMENTATION GENERATOR
# ============================================================================

def generate_api_docs_html(workspace_path):
    """Generate a full HTML API documentation portal from collections."""
    collections = {}
    col_dir = f"{workspace_path}/collections"
    if os.path.isdir(col_dir):
        for f in os.listdir(col_dir):
            if f.endswith(".json"):
                name = os.path.splitext(f)[0]
                with open(f"{col_dir}/{f}") as fh:
                    try:
                        collections[name] = json.load(fh)
                    except Exception:
                        pass

    # Build endpoint cards
    endpoint_cards = ""
    total_endpoints = 0
    for col_name, col_data in collections.items():
        requests = col_data.get("requests", [])
        if isinstance(col_data, dict) and "url" in col_data and not requests:
            requests = [col_data]

        if not requests:
            continue

        cards = ""
        for req in requests:
            total_endpoints += 1
            method = req.get("method", "GET")
            method_colors = {"GET": "#00e676", "POST": "#64b5f6", "PUT": "#ffb74d",
                             "PATCH": "#ce93d8", "DELETE": "#ff5252", "HEAD": "#90a4ae"}
            color = method_colors.get(method.upper(), "#888")
            url = req.get("url", "")
            name = req.get("name", url)
            body = req.get("body", "")
            headers = req.get("headers", "")
            if isinstance(headers, list):
                headers = "\n".join(headers)

            body_block = ""
            if body:
                try:
                    formatted = json.dumps(json.loads(body), indent=2)
                except Exception:
                    formatted = body
                body_block = f'<div style="margin-top:8px"><span style="color:#8b949e;font-size:11px">Request Body:</span><pre style="background:#0d1117;padding:8px;border-radius:6px;font-size:11px;overflow-x:auto">{formatted}</pre></div>'

            header_block = ""
            if headers and headers.strip():
                header_block = f'<div style="margin-top:8px"><span style="color:#8b949e;font-size:11px">Headers:</span><pre style="background:#0d1117;padding:8px;border-radius:6px;font-size:11px">{headers}</pre></div>'

            cards += f"""<div style="background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px;margin-bottom:12px">
                <div style="display:flex;align-items:center;gap:10px">
                    <span style="background:{color}22;color:{color};padding:3px 10px;border-radius:4px;font-weight:700;font-size:12px;font-family:monospace">{method}</span>
                    <span style="color:#e6edf3;font-weight:600">{name}</span>
                </div>
                <div style="color:#64b5f6;font-family:monospace;font-size:12px;margin-top:6px">{url}</div>
                {header_block}{body_block}
            </div>"""

        endpoint_cards += f"""<div style="margin-bottom:32px">
            <h2 style="color:#58a6ff;border-bottom:1px solid #30363d;padding-bottom:8px">📁 {col_name}</h2>
            {cards}</div>"""

    html = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>API Documentation — LightBase</title>
<style>
body{{background:#0d1117;color:#e6edf3;font-family:'Segoe UI',sans-serif;margin:0;padding:0}}
.header{{background:linear-gradient(135deg,#0d1117,#161b22);padding:40px;border-bottom:1px solid #30363d}}
.content{{max-width:900px;margin:0 auto;padding:32px}}
h1{{color:#58a6ff;margin:0}} h3{{color:#8b949e;font-weight:400;margin:8px 0 0}}
pre{{color:#e6edf3;margin:0}}
</style></head><body>
<div class="header">
  <div style="max-width:900px;margin:0 auto">
    <h1>📚 API Documentation</h1>
    <h3>Auto-generated from LightBase collections • {total_endpoints} endpoints • {len(collections)} collections</h3>
    <div style="margin-top:16px;color:#484f58;font-size:12px">Generated: {datetime.utcnow().isoformat()}Z</div>
  </div>
</div>
<div class="content">{endpoint_cards}</div>
<div style="text-align:center;color:#484f58;padding:40px;font-size:12px">
  Powered by LightBase Enterprise Engine
</div></body></html>"""

    # Save to workspace
    out_path = f"{workspace_path}/docs/html/api_docs.html"
    with open(out_path, 'w') as f:
        f.write(html)

    return {"path": out_path, "total_endpoints": total_endpoints, "collections": len(collections)}


# ============================================================================
# 7. CODE SNIPPET GENERATOR
# ============================================================================

def generate_code_snippet(method, url, headers, body, language):
    """Generate client code snippets in various languages from a request."""
    headers_dict = {}
    if isinstance(headers, str) and headers.strip():
        for line in headers.split("\r\n"):
            if ":" in line:
                k, v = line.split(":", 1)
                headers_dict[k.strip()] = v.strip()
    elif isinstance(headers, list):
        for h in headers:
            if ":" in str(h):
                k, v = str(h).split(":", 1)
                headers_dict[k.strip()] = v.strip()

    lang = language.lower()

    if lang == "curl":
        parts = [f"curl -X {method} '{url}'"]
        for k, v in headers_dict.items():
            parts.append(f"  -H '{k}: {v}'")
        if body:
            parts.append(f"  -d '{body}'")
        return " \\\n".join(parts)

    elif lang == "python":
        h_str = json.dumps(headers_dict, indent=4) if headers_dict else "{}"
        code = f'import requests\n\nresponse = requests.{method.lower()}(\n    "{url}",\n    headers={h_str}'
        if body:
            try:
                json.loads(body)
                code += f',\n    json={body}'
            except Exception:
                code += f',\n    data="""{body}"""'
        code += '\n)\n\nprint(response.status_code)\nprint(response.json())'
        return code

    elif lang in ("javascript", "js", "node"):
        h_str = json.dumps(headers_dict, indent=4) if headers_dict else "{}"
        code = f"""const response = await fetch("{url}", {{
    method: "{method}",
    headers: {h_str}"""
        if body:
            code += f',\n    body: JSON.stringify({body})'
        code += f"""
}});

const data = await response.json();
console.log(data);"""
        return code

    elif lang == "java":
        code = f"""HttpClient client = HttpClient.newHttpClient();
HttpRequest request = HttpRequest.newBuilder()
    .uri(URI.create("{url}"))
    .method("{method}", """
        if body:
            code += f'HttpRequest.BodyPublishers.ofString("{body}"))'
        else:
            code += 'HttpRequest.BodyPublishers.noBody())'
        for k, v in headers_dict.items():
            code += f'\n    .header("{k}", "{v}")'
        code += f"""
    .build();

HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
System.out.println(response.body());"""
        return code

    elif lang == "go":
        code = f"""package main

import (
    "fmt"
    "net/http"
    "io/ioutil"
"""
        if body:
            code += '    "strings"\n'
        code += """)

func main() {
"""
        if body:
            code += f'    body := strings.NewReader(`{body}`)\n'
            code += f'    req, _ := http.NewRequest("{method}", "{url}", body)\n'
        else:
            code += f'    req, _ := http.NewRequest("{method}", "{url}", nil)\n'
        for k, v in headers_dict.items():
            code += f'    req.Header.Set("{k}", "{v}")\n'
        code += """    resp, _ := http.DefaultClient.Do(req)
    defer resp.Body.Close()
    data, _ := ioutil.ReadAll(resp.Body)
    fmt.Println(string(data))
}"""
        return code

    elif lang == "php":
        code = f"""<?php
$ch = curl_init("{url}");
curl_setopt($ch, CURLOPT_CUSTOMREQUEST, "{method}");
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);"""
        if headers_dict:
            h_arr = ", ".join([f'"{k}: {v}"' for k, v in headers_dict.items()])
            code += f"\ncurl_setopt($ch, CURLOPT_HTTPHEADER, [{h_arr}]);"
        if body:
            code += f'\ncurl_setopt($ch, CURLOPT_POSTFIELDS, \'{body}\');'
        code += """
$response = curl_exec($ch);
curl_close($ch);
echo $response;
?>"""
        return code

    return f"// Language '{language}' not supported. Available: curl, python, javascript, java, go, php"


# ============================================================================
# 8. AUTH HANDLER (OAuth 2.0, Bearer, Basic, API Key)
# ============================================================================

def apply_auth_headers(headers_str, auth_config):
    """Apply authentication headers based on auth type configuration."""
    if not auth_config:
        return headers_str

    auth_type = auth_config.get("type", "").lower()
    lines = headers_str.split("\r\n") if headers_str else []

    if auth_type == "bearer":
        token = auth_config.get("token", "")
        lines.append(f"Authorization: Bearer {token}")

    elif auth_type == "basic":
        import base64
        username = auth_config.get("username", "")
        password = auth_config.get("password", "")
        encoded = base64.b64encode(f"{username}:{password}".encode()).decode()
        lines.append(f"Authorization: Basic {encoded}")

    elif auth_type == "api_key":
        key_name = auth_config.get("key", "X-API-Key")
        key_value = auth_config.get("value", "")
        location = auth_config.get("in", "header")
        if location == "header":
            lines.append(f"{key_name}: {key_value}")
        # If "query", the caller should append to URL params

    elif auth_type == "oauth2":
        access_token = auth_config.get("access_token", "")
        if access_token:
            lines.append(f"Authorization: Bearer {access_token}")

    return "\r\n".join([l for l in lines if l.strip()])


# ============================================================================
# 9. CSV/JSON DATA-DRIVEN TEST ITERATIONS
# ============================================================================

def parse_iteration_data(data_content, data_format="csv"):
    """Parse CSV or JSON iteration data into list of variable dictionaries."""
    rows = []

    if data_format == "csv":
        import csv, io
        reader = csv.DictReader(io.StringIO(data_content))
        for row in reader:
            rows.append(dict(row))

    elif data_format == "json":
        parsed = json.loads(data_content)
        if isinstance(parsed, list):
            rows = parsed
        elif isinstance(parsed, dict):
            rows = [parsed]

    return rows


def run_collection_data_driven(collection_data, iteration_data, base_env, lightbase_lib, encode_tlv_fn):
    """Run a collection once per row of iteration data (CSV/JSON driven)."""
    all_results = []
    total_passed = 0
    total_failed = 0
    start_time = time.time()

    for i, row_vars in enumerate(iteration_data):
        merged_env = dict(base_env) if base_env else {}
        merged_env.update(row_vars)

        result = run_collection(collection_data, merged_env, 1, lightbase_lib, encode_tlv_fn)
        result["data_row"] = i + 1
        result["data_vars"] = row_vars
        total_passed += result["total_passed"]
        total_failed += result["total_failed"]
        all_results.append(result)

    return {
        "data_driven": True,
        "total_rows": len(iteration_data),
        "total_passed": total_passed,
        "total_failed": total_failed,
        "total_time_ms": round((time.time() - start_time) * 1000, 2),
        "row_results": all_results,
        "success": total_failed == 0,
    }


# ============================================================================
# 10. WEBHOOK ALERTS (Slack / Teams / Generic)
# ============================================================================

def send_webhook_alert(webhook_url, event_type, details, platform="generic"):
    """Send alert to Slack, Microsoft Teams, or a generic webhook."""
    import urllib.request

    if platform == "slack":
        color = "#00e676" if details.get("success") else "#ff5252"
        payload = {
            "attachments": [{
                "color": color,
                "title": f"LightBase: {event_type}",
                "text": details.get("message", ""),
                "fields": [
                    {"title": "Collection", "value": details.get("collection", "—"), "short": True},
                    {"title": "Passed/Failed", "value": f"{details.get('passed',0)}/{details.get('failed',0)}", "short": True},
                    {"title": "Duration", "value": f"{details.get('duration_ms',0)}ms", "short": True},
                ],
                "footer": "LightBase Enterprise Engine",
                "ts": int(time.time()),
            }]
        }
    elif platform == "teams":
        color = "00e676" if details.get("success") else "ff5252"
        payload = {
            "@type": "MessageCard",
            "themeColor": color,
            "title": f"LightBase: {event_type}",
            "text": details.get("message", ""),
            "sections": [{
                "facts": [
                    {"name": "Collection", "value": details.get("collection", "—")},
                    {"name": "Passed", "value": str(details.get("passed", 0))},
                    {"name": "Failed", "value": str(details.get("failed", 0))},
                    {"name": "Duration", "value": f"{details.get('duration_ms',0)}ms"},
                ]
            }]
        }
    else:
        payload = {"event": event_type, "details": details, "timestamp": datetime.utcnow().isoformat()}

    try:
        data = json.dumps(payload).encode()
        req = urllib.request.Request(webhook_url, data=data, headers={"Content-Type": "application/json"})
        resp = urllib.request.urlopen(req, timeout=10)
        return {"status": "sent", "response_code": resp.getcode()}
    except Exception as e:
        return {"status": "error", "error": str(e)}


# ============================================================================
# 11. COLLECTION COMMENTS & FORKING
# ============================================================================

def add_collection_comment(workspace_path, collection_name, comment_text, author="anonymous"):
    """Add a review comment to a collection."""
    comments_dir = f"{workspace_path}/comments"
    os.makedirs(comments_dir, exist_ok=True)
    comments_file = f"{comments_dir}/{collection_name}.json"

    comments = []
    if os.path.exists(comments_file):
        with open(comments_file) as f:
            comments = json.load(f)

    comments.append({
        "id": str(uuid.uuid4())[:8],
        "author": author,
        "text": comment_text,
        "timestamp": datetime.utcnow().isoformat() + "Z",
    })

    with open(comments_file, 'w') as f:
        json.dump(comments, f, indent=2)

    return {"comment_count": len(comments), "collection": collection_name}


def get_collection_comments(workspace_path, collection_name):
    """Retrieve all comments for a collection."""
    comments_file = f"{workspace_path}/comments/{collection_name}.json"
    if os.path.exists(comments_file):
        with open(comments_file) as f:
            return json.load(f)
    return []


def fork_collection(workspace_path, source_name, fork_name, author="anonymous"):
    """Fork a collection (like Git fork) for independent development."""
    source = f"{workspace_path}/collections/{source_name}.json"
    if not os.path.exists(source):
        return {"error": f"Collection '{source_name}' not found"}

    with open(source) as f:
        data = json.load(f)

    data["_forked_from"] = source_name
    data["_forked_at"] = datetime.utcnow().isoformat() + "Z"
    data["_forked_by"] = author
    data["name"] = fork_name

    target = f"{workspace_path}/collections/{fork_name}.json"
    with open(target, 'w') as f:
        json.dump(data, f, indent=2)

    return {"forked": True, "source": source_name, "fork": fork_name, "path": target}


# ============================================================================
# 10. WORKFLOW CONTROL — setNextRequest() SUPPORT
# ============================================================================

def run_collection_with_workflow(collection_data, env_vars, lightbase_lib, encode_tlv_fn):
    """
    Run a collection with Postman-style setNextRequest() workflow control.
    Each request's test_script can call setNextRequest("name") to jump to
    a specific request, skip requests, or loop back.
    Max execution depth protects against infinite loops.
    """
    requests = collection_data.get("requests", [])
    if isinstance(collection_data, dict) and "url" in collection_data and not requests:
        requests = [collection_data]

    if not requests:
        return {"error": "No requests in collection", "results": [], "total_passed": 0, "total_failed": 0}

    # Build name → index lookup
    name_index = {}
    for i, req in enumerate(requests):
        name = req.get("name", f"Request_{i+1}")
        name_index[name] = i

    run_env = dict(env_vars) if env_vars else {}
    results = []
    total_passed = 0
    total_failed = 0
    start_time = time.time()

    current_index = 0
    max_steps = len(requests) * 10  # Safety: prevent infinite loops
    steps = 0

    while current_index < len(requests) and steps < max_steps:
        steps += 1
        req = requests[current_index]
        req_name = req.get("name", f"Request_{current_index+1}")

        # Execute the request
        result = execute_single_request(req, run_env, lightbase_lib, encode_tlv_fn)

        # Variable extraction (chaining)
        extractions = req.get("extract", {})
        for var_name, json_path in extractions.items():
            if result["response_json"]:
                extracted = jsonpath_extract(result["response_json"], json_path)
                if extracted is not None:
                    run_env[var_name] = extracted

        # Run test script — check for setNextRequest in the script
        test_script = req.get("test_script", req.get("tests", ""))
        test_result = run_test_script(
            test_script, result["response_json"],
            result["response_body"], result["status_code"],
            lightbase_lib
        )
        result["test_results"] = test_result
        total_passed += test_result["passed"]
        total_failed += test_result["failed"]

        # Check for setNextRequest() in the test script (parse from script text)
        next_request = None
        if test_script:
            import re as _re
            match = _re.search(r'setNextRequest\s*\(\s*["\'](.+?)["\']\s*\)', test_script)
            if match:
                next_request = match.group(1)

        result["workflow_step"] = steps
        result["next_request"] = next_request
        results.append(result)

        # Determine next request
        if next_request is not None:
            if next_request.lower() == "null" or next_request == "":
                break  # Stop execution
            elif next_request in name_index:
                current_index = name_index[next_request]
            else:
                # Name not found, advance normally
                current_index += 1
        else:
            current_index += 1

    total_time = round((time.time() - start_time) * 1000, 2)

    return {
        "collection_name": collection_data.get("name", "Unnamed"),
        "timestamp": datetime.utcnow().isoformat() + "Z",
        "workflow_mode": True,
        "total_requests": len(results),
        "total_steps": steps,
        "total_passed": total_passed,
        "total_failed": total_failed,
        "total_time_ms": total_time,
        "results": results,
        "final_environment": run_env,
        "success": total_failed == 0,
    }


# ============================================================================
# 11. COLLECTION-LEVEL PRE-REQUEST / TEST SCRIPTS
# ============================================================================

def run_collection_with_scripts(collection_data, env_vars, iterations,
                                 collection_prereq_script, collection_test_script,
                                 lightbase_lib, encode_tlv_fn):
    """
    Run a collection with collection-level pre-request and test scripts.
    The collection-level pre-request script runs BEFORE each request.
    The collection-level test script runs AFTER each request (in addition to per-request tests).
    This follows Postman's DRY principle for shared logic.
    """
    requests = collection_data.get("requests", [])
    if isinstance(collection_data, dict) and "url" in collection_data and not requests:
        requests = [collection_data]

    all_iterations = []
    total_passed = 0
    total_failed = 0
    total_requests = 0
    start_time = time.time()

    run_env = dict(env_vars) if env_vars else {}

    for iteration in range(max(1, iterations)):
        iteration_results = []

        for req in requests:
            # Run collection-level pre-request script (Python)
            if collection_prereq_script:
                try:
                    prereq_ns = {"env": run_env, "json": json, "os": os, "time": time}
                    exec(collection_prereq_script, prereq_ns)
                    # Allow the script to modify env
                    if "env" in prereq_ns and isinstance(prereq_ns["env"], dict):
                        run_env.update(prereq_ns["env"])
                except Exception as e:
                    print(f"[Enterprise] Collection pre-req script error: {e}")

            # Execute request with current env
            result = execute_single_request(req, run_env, lightbase_lib, encode_tlv_fn)
            total_requests += 1

            # Variable extraction (chaining)
            extractions = req.get("extract", {})
            for var_name, json_path in extractions.items():
                if result["response_json"]:
                    extracted = jsonpath_extract(result["response_json"], json_path)
                    if extracted is not None:
                        run_env[var_name] = extracted

            # Run per-request test script
            test_script = req.get("test_script", req.get("tests", ""))
            test_result = run_test_script(
                test_script, result["response_json"],
                result["response_body"], result["status_code"],
                lightbase_lib
            )

            # Run collection-level test script (appended to per-request tests)
            col_test_result = {"passed": 0, "failed": 0, "log": ""}
            if collection_test_script:
                col_test_result = run_test_script(
                    collection_test_script, result["response_json"],
                    result["response_body"], result["status_code"],
                    lightbase_lib
                )

            combined_passed = test_result["passed"] + col_test_result["passed"]
            combined_failed = test_result["failed"] + col_test_result["failed"]
            combined_log = test_result["log"]
            if col_test_result["log"]:
                combined_log += "\n--- Collection-Level Tests ---\n" + col_test_result["log"]

            result["test_results"] = {
                "passed": combined_passed,
                "failed": combined_failed,
                "log": combined_log
            }
            total_passed += combined_passed
            total_failed += combined_failed

            iteration_results.append(result)

        all_iterations.append({
            "iteration": iteration + 1,
            "results": iteration_results,
        })

    total_time = round((time.time() - start_time) * 1000, 2)

    return {
        "collection_name": collection_data.get("name", "Unnamed"),
        "timestamp": datetime.utcnow().isoformat() + "Z",
        "collection_scripts": True,
        "total_requests": total_requests,
        "total_passed": total_passed,
        "total_failed": total_failed,
        "total_time_ms": total_time,
        "iterations": all_iterations,
        "final_environment": run_env,
        "success": total_failed == 0,
    }
