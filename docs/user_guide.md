# LightBase Studio — User Guide

> A local-first, multi-protocol API development platform powered by a native C-Core engine.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Dashboard](#dashboard)
3. [API Studio](#api-studio)
   - [REST Requests](#rest-requests)
   - [GraphQL](#graphql)
   - [WebSocket](#websocket)
   - [gRPC](#grpc)
   - [MQTT](#mqtt)
4. [Pre-Request Scripts](#pre-request-scripts)
5. [Test Scripts](#test-scripts)
6. [Response Visualizer](#response-visualizer)
7. [SQL Console](#sql-console)
8. [Visual Flows](#visual-flows)
9. [Monitors](#monitors)
10. [Request History](#request-history)
11. [Collections & Local Storage](#collections--local-storage)
12. [Environments & Variables](#environments--variables)
13. [Mock Servers](#mock-servers)
14. [API Documentation Generator](#api-documentation-generator)
15. [AI Companion](#ai-companion)
16. [Crypto Vault](#crypto-vault)
17. [Git Sync](#git-sync)
18. [Cookie Jar](#cookie-jar)
19. [Stress Testing](#stress-testing)

---

## Getting Started

### Prerequisites

- **C-Core**: Build with `cd core && mkdir -p build_release && cd build_release && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build .`
- **Python Dependencies**: `pip install websocket-client paho-mqtt grpcio grpcio-tools`
- **Start Bridge**: `cd bridge && python python_bridge.py`
- **Open UI**: Open `ui/index.html` in any modern browser

### Architecture

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   Browser UI    │ ←→  │  Python Bridge   │ ←→  │  C-Core Engine  │
│  (HTML/CSS/JS)  │     │  (port 8000)     │     │  (libcore.so)   │
└─────────────────┘     └──────────────────┘     └─────────────────┘
                              │
                        ┌─────┴──────┐
                        │ workspace/ │  ← 100% local JSON files
                        │  (Git-able)│
                        └────────────┘
```

### 100% Local-First Storage

All your data is stored as plain JSON files in `workspace/`:

```
workspace/
├── collections/    # Saved API requests
├── environments/   # Variable sets (dev, staging, prod)
├── history/        # Auto-logged request/response pairs
├── monitors/       # Scheduled run configs & results
├── mocks/          # Mock server definitions
├── flows/          # Visual workflow definitions
└── docs/           # Generated OpenAPI specs
```

**Every file is a plain JSON file you can commit to Git.** No cloud sync needed — just `git push`.

---

## Dashboard

The Dashboard tab shows real-time system telemetry from the native C-Core:

- **CPU Utilization**: Live percentage with gradient area chart (30-point history)
- **Memory Usage**: Used/Total with animated progress bar
- **Ring Buffer Slot**: Current telemetry log write position
- **Telemetry History**: Click "Load" to read stored binary telemetry snapshots from the C mmap'd ring buffer

---

## API Studio

The API Studio supports **5 protocols** via the protocol selector bar at the top.

### REST Requests

1. Select HTTP method: **GET**, **POST**, **PUT**, **DELETE**, **PATCH**
2. Enter URL — supports `{{variable}}` interpolation from your active environment
3. Add **Headers** and **Form Data** using the ＋ buttons
4. Enter JSON **Body** for POST/PUT/PATCH
5. Click **Send**

The response appears below with:
- Status badge (color-coded)
- Latency (µs from C-Core)
- Size (bytes)
- Hex dump (expandable)
- Filter box (regex-capable with `/pattern/flags` syntax)

### GraphQL

1. Switch to the **GraphQL** protocol tab
2. Enter the GraphQL endpoint hostname and path
3. Write your query in the Query editor
4. Add variables in the Variables panel (JSON)
5. Click **Query**

```graphql
query GetUser($id: ID!) {
  user(id: $id) {
    name
    email
    posts { title }
  }
}
```

Variables:
```json
{"id": "123"}
```

### WebSocket

1. Switch to the **WebSocket** protocol tab
2. Enter a WebSocket URL (e.g., `wss://echo.websocket.org`)
3. Click **Connect** to establish connection
4. Type messages and click **Send** — responses appear in the live message log
5. All messages are timestamped and logged

### gRPC

1. Switch to the **gRPC** protocol tab
2. Enter the target address (e.g., `localhost:50051`)
3. Specify the **Service** name (e.g., `helloworld.Greeter`)
4. Specify the **Method** name (e.g., `SayHello`)
5. Enter the message as JSON
6. Click **Invoke**

### MQTT

1. Switch to the **MQTT** protocol tab
2. Enter broker address and port
3. Set the **Topic** (e.g., `sensors/temperature`)
4. **Publish**: Enter a payload and click Publish
5. **Subscribe**: Click Subscribe to listen for 3 seconds and display received messages

---

## Pre-Request Scripts

Write JavaScript that executes **before each REST request**. Use the `lb` object:

```javascript
// Set a dynamic timestamp
lb.setVariable('timestamp', Date.now().toString());

// Generate a random request ID
lb.setVariable('requestId', Math.random().toString(36).substr(2, 9));

// Read an existing variable
const token = lb.getVariable('authToken');
```

Variables set here are immediately available as `{{variableName}}` in your URL, headers, and body.

---

## Test Scripts

Write test scripts using the `lb.*` API that runs **after each response**:

```javascript
// Named test blocks
lb.test("Status is 200", () => {
    lb.expect(lb.response.status).toBe(200);
});

lb.test("Response has data", () => {
    const body = lb.response.json();
    lb.expect(body.url).toBeTruthy();
});

lb.test("Has correct header", () => {
    lb.expect(lb.response.status).toBeGreaterThan(199);
});

// Set variables for chaining across requests
lb.setVariable('userId', lb.response.json().id);
```

### Available Assertions

| Method | Description |
|--------|-------------|
| `lb.expect(val).toBe(x)` | Strict equality check |
| `lb.expect(val).toContain(x)` | String/array contains |
| `lb.expect(val).toBeTruthy()` | Truthy check |
| `lb.expect(val).toBeGreaterThan(x)` | Numeric comparison |

### Available Objects

| Object | Description |
|--------|-------------|
| `lb.response.status` | HTTP status code |
| `lb.response.json()` | Parsed response body |
| `lb.response.headers` | Response headers object |
| `lb.setVariable(k, v)` | Store cross-request variable |
| `lb.getVariable(k)` | Retrieve stored variable |

---

## Response Visualizer

Create custom HTML/CSS/JS dashboards from API response data:

1. Expand the **Visualizer** section below the response
2. Write HTML/JS using the `data` variable (which contains the parsed response)
3. Click **▶ Render** to display in the sandboxed iframe

```javascript
// Example: Create a table from API data
const items = Array.isArray(data) ? data : [data];
let html = '<table border="1" style="border-collapse:collapse;font-family:sans-serif">';
html += '<tr><th>Key</th><th>Value</th></tr>';
Object.entries(items[0] || {}).forEach(([k,v]) => {
    html += `<tr><td>${k}</td><td>${v}</td></tr>`;
});
html += '</table>';
document.body.innerHTML = html;
```

---

## SQL Console

Execute SQL queries directly against your active database through the C-Core arena allocator:

1. Switch to the **SQL** tab
2. Write one or more SQL statements
3. Click **⚡ Execute**
4. Results appear as formatted JSON with C-Core latency displayed

```sql
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    email TEXT UNIQUE
);

INSERT INTO users (name, email) VALUES ('Aarav', 'aarav@lightbase.dev');
SELECT * FROM users;
```

---

## Visual Flows

Build complex, multi-step API workflows visually:

### Node Types

| Node | Purpose |
|------|---------|
| **Request** | Executes an HTTP request (method + URL) |
| **Condition** | Evaluates a JavaScript expression to branch logic |
| **Transform** | Transforms data between steps using JavaScript |
| **AI Block** | Processes data through the local AI inference engine |

### Usage

1. Switch to the **Flows** tab
2. Click **＋ Request**, **＋ Condition**, **＋ Transform**, or **＋ AI Block** to add nodes
3. Configure each node (URL, condition expression, transform code, or AI prompt)
4. Enter a flow name and click **💾 Save** (saves to `workspace/flows/`)
5. Click **▶ Run** to execute all nodes sequentially
6. Results appear in the output pane

Saved flows appear in the list below and can be reloaded by clicking them.

---

## Monitors

Schedule automated collection runs to continuously verify API health:

1. Switch to the **Monitors** tab
2. Select a collection from the dropdown
3. Set the interval (seconds)
4. **Run Once**: Execute the collection immediately and see results
5. **Schedule**: Start a recurring background monitor
6. Active monitors appear in the list with a Stop button

Monitor results are saved to `workspace/monitors/` as JSON files with timestamps.

---

## Request History

Every request across all protocols is automatically logged:

- **Auto-saved** to `workspace/history/` as individual JSON files
- **Protocol badges**: REST (blue), GraphQL (purple), WebSocket (cyan), MQTT (green), gRPC (yellow)
- **Search**: Filter by URL or protocol
- **Click to restore**: Click any history item to load it back into the API Studio
- **Clear**: Remove all history with the 🗑 button

---

## Collections & Local Storage

### Saving a Collection

1. Enter a name in the **Collections** sidebar input
2. Click **＋ Save** — this saves the current request configuration (URL, method, headers, body, test scripts, pre-request scripts)
3. Saved as `workspace/collections/{name}.json`

### Loading a Collection

Click any collection name in the sidebar to restore all fields.

### Git Collaboration

Since everything is plain JSON in `workspace/`, just add it to your `.gitignore` selectively or commit everything:

```bash
git add workspace/collections/ workspace/environments/
git commit -m "Add API test collections"
git push
```

Your teammates get identical collections, environments, and flows by pulling.

---

## Environments & Variables

### Built-in Environments

| Environment | DB File | Description |
|-------------|---------|-------------|
| 🟢 Development | `test_lightbase.db` | Local development |
| 🟡 Staging | `staging_lightbase.db` | Pre-production testing |
| 🔴 Production | `prod_lightbase.sovereign.db` | Live system |

### Variable Interpolation

Use `{{variableName}}` anywhere in URLs, headers, or body:

```
https://{{baseUrl}}/api/v1/users
Authorization: {{authToken}}
```

### Custom Variables

1. Click **✏️ Edit Variables** in the sidebar
2. Add/modify key-value pairs
3. Click **💾 Save** — persisted to `workspace/environments/`
4. Variables are available immediately as `{{key}}` tokens

---

## Mock Servers

Create simulated API endpoints for frontend development:

### Using mock:// Protocol

1. Register mocks by entering a path and response body in the Mock Engine sidebar
2. Use `mock://server/path` as your URL in API Studio
3. LightBase will intercept the request and return the mocked response locally

### Persistent Mocks via Bridge

Mocks can also be registered via the bridge at `POST /mock/register`:

```json
{
    "name": "user-service",
    "routes": [
        {"path": "/users", "method": "GET", "status": 200, "body": [{"id": 1, "name": "Test"}]},
        {"path": "/users", "method": "POST", "status": 201, "body": {"id": 2}}
    ]
}
```

---

## API Documentation Generator

### Schema Documentation

Click **🛠️ Schema Docs** to auto-generate a Markdown specification of all database tables, columns, types, and constraints from the active database.

### OpenAPI 3.0 Export

Click **📋 Export OpenAPI** to auto-generate an OpenAPI 3.0 spec from all your saved collections. The spec is saved to `workspace/docs/openapi_spec.json`.

---

## AI Companion

The built-in AI Companion uses local offline inference (llama.cpp) via the C-Core:

1. Switch to the **AI** tab
2. Enter any prompt — the AI automatically receives your current database schema as context
3. Click **Infer**

Use it for:
- Writing SQL queries from natural language
- Generating test scripts
- Optimizing database schemas
- Understanding code patterns

---

## Crypto Vault

Store API keys with AES-256-GCM encryption at the C level:

1. Select a provider (OpenAI, Anthropic, Gemini)
2. Enter the plaintext API key
3. Click **🔒 Store** — the key is encrypted by the C-Core and stored in the database
4. Keys can be decrypted via `POST /retrieve_vault_key`

---

## Git Sync

Click **↻** next to **Git Sync** in the sidebar to inspect your repository status using the native libgit2 C binding. Shows current branch, uncommitted changes, and tracking status.

---

## Stress Testing

Load-test your APIs with configurable burst patterns:

1. Set **Count** (number of requests) and **Delay** (milliseconds between)
2. Click **🔥 Ignite**
3. Watch real-time latency measurements from the C-Core
4. Average latency is calculated at completion

---

## Keyboard Shortcuts & Tips

- **Sidebar toggle**: Click ☰ to collapse/expand the sidebar
- **Variable preview**: Hover over `{{tokens}}` to see resolved values
- **Response filter**: Use `/regex/i` syntax for regex filtering
- **History click**: Click any history item to jump back to API Studio with that request loaded
- **Flow nodes**: Click ✕ on any flow node to remove it

---

## CI/CD Integration

Export your collections and run them headlessly:

```bash
# Run a collection via the bridge API
curl -X POST http://localhost:8000/run_monitor \
  -H "Content-Type: application/json" \
  -d '{"collection": "my_api_tests"}'

# Export OpenAPI spec
curl -X POST http://localhost:8000/export_openapi \
  -H "Content-Type: application/json" -o openapi.json

# Use in GitHub Actions, Jenkins, etc.
```

All collection files in `workspace/collections/` can be version-controlled and executed in your CI pipeline.

---

## Python Plugin System

Write Python scripts with full PyPI access to process API data:

1. Switch to the **Plugins** tab
2. Write a script with a `run(context)` function
3. Click **💾 Save** then **▶ Run**

```python
import json

def run(context):
    """Process API response data"""
    data = context.get("response", {})
    return {
        "key_count": len(data),
        "keys": list(data.keys()),
        "has_errors": "error" in str(data).lower()
    }
```

### Installing Packages

Use the **📦 Package Installer** to install any PyPI package:
- Enter package names (e.g., `pandas`, `numpy`, `beautifulsoup4`)
- Click **Install** — runs `pip install` on the server

### Plugin Context

The `context` dict contains:

| Key | Description |
|-----|-------------|
| `url` | Current request URL |
| `method` | HTTP method |
| `body` | Request body |
| `response` | Parsed response data |

---

## AI Agentic Workflows

Three AI-powered actions in the API Studio, all running 100% locally:

### 🤖 AI Generate Tests

Reads the current response and auto-generates `lb.test()` assertions. Click the button next to "Test Scripts" — generated tests populate the editor automatically.

### 🔗 Chain Next

AI analyzes the response and suggests the next logical API request. Automatically fills in the URL, method, and body fields. Perfect for API exploration.

### 💡 Explain

AI explains the current response in plain English, highlighting important fields, potential issues, and data patterns.

**All processing is local** — zero data leaves your machine.

---

## Live Data Streamer

Stream high-volume WebSocket and SSE data in real-time:

1. In API Studio, switch to the **Streams** protocol tab
2. Select **WebSocket** or **SSE** from the dropdown
3. Enter the stream URL
4. Set **Duration** (seconds to listen)
5. Click **▶ Stream**

The dashboard shows:
- **Message count** and **rate per second**
- **Live data log** with all received messages
- **Message inspector** for detailed examination

Supports up to 200 messages per session without UI freezing.

---

## Jupyter Notebook Export

One-click export to `.ipynb` or `.py`:

### From History
Click **📓 Export .ipynb** or **🐍 Export .py** in the History tab toolbar. Exports the last 20 requests as executable code.

### From Collections
Export saved collections as notebooks with proper `requests` library calls.

Generated files are saved to `workspace/exports/` and can be opened in Jupyter Lab, VS Code, or any notebook environment.

### Example Output (.py)
```python
#!/usr/bin/env python3
"""Generated by LightBase Studio"""
import requests, json

# Request 1
r0 = requests.get('https://httpbin.org/get')
print(f'Status: {r0.status_code}')
print(json.dumps(r0.json(), indent=2))
```

---

*Built with ⚡ by LightBase — 100% local, zero cloud dependencies.*

