# ⚡ LightBase

**A local-first, multi-protocol API development platform powered by a native C-Core engine.**

LightBase is a zero-cloud API development studio that combines a high-performance C backend with a Python gateway bridge and a sleek browser-based UI. Every request, collection, and environment is stored as plain JSON files you can version-control with Git — no accounts, no cloud sync, no telemetry.

---

## ✨ Features

| Feature | Description |
|---------|-------------|
| **Multi-Protocol API Studio** | REST, GraphQL, WebSocket, gRPC, MQTT — all in one tool |
| **Native C-Core Engine** | OpenSSL TLS, arena memory allocators, multi-threaded IPC via Unix sockets |
| **SQL Console** | Execute queries against local SQLite with sub-millisecond C-Core latency |
| **Python Plugin System** | Write Python scripts with full PyPI access to process API responses |
| **Visual Flow Builder** | Chain requests, conditions, transforms, and AI blocks into automated workflows |
| **Live Data Streamer** | Real-time WebSocket and SSE streaming with message rate tracking |
| **Crypto Vault** | AES-256-GCM encrypted API key storage at the C level |
| **AI Companion** | Local llama.cpp inference for test generation, request chaining, and response explanations |
| **Jupyter & Python Export** | One-click export of history or collections to `.ipynb` or `.py` |
| **OpenAPI Generator** | Auto-generate OpenAPI 3.0 specs from saved collections |
| **Stress Testing** | Burst-pattern load testing with real-time C-Core latency tracking |
| **Monitors** | Scheduled collection runners for continuous API health checks |
| **Environment Variables** | `{{variable}}` interpolation across URLs, headers, and bodies |
| **Git-Versionable Workspace** | Everything in `workspace/` is plain JSON — just `git push` |

---

## 🏗️ Architecture

```
┌─────────────────┐     ┌──────────────────────┐     ┌──────────────────────┐
│   Browser UI    │ ←→  │   Python Bridge       │ ←→  │   C-Core Engine      │
│  (HTML/CSS/JS)  │HTTP │   (port 8000)         │ IPC │   (libcore.so)       │
│                 │     │                        │ UDS │                      │
│  • API Studio   │     │  • Route dispatcher    │     │  • OpenSSL TLS 1.3   │
│  • SQL Console  │     │  • Plugin executor     │     │  • Arena allocators   │
│  • Flow Builder │     │  • History logger      │     │  • Thread pool (8)    │
│  • Dashboards   │     │  • Export engine        │     │  • mmap telemetry    │
└─────────────────┘     └──────────────────────┘     └──────────────────────┘
                               │                           │
                         ┌─────┴──────┐              /tmp/lightbase.sock
                         │ workspace/ │              (TLV binary protocol)
                         │  (Git-able)│
                         └────────────┘
```

### Layer Responsibilities

- **Frontend UI** — Lightweight HTML/CSS/JS client with glassmorphism design, protocol tabs, and real-time dashboards
- **Python Bridge** — Zero-dependency HTTP gateway (port 8000) that proxies requests to the C-Core over Unix Domain Sockets, runs Python plugins, manages filesystem storage, and handles exports
- **C-Core (`libcore.so`)** — Multi-threaded native engine handling TLS networking, SQLite queries, telemetry logging, AES crypto, Git status, and AI inference via a binary TLV protocol over `/tmp/lightbase.sock`

---

## 🚀 Quick Start

### Prerequisites

```bash
# Build tools & dependencies
sudo apt update && sudo apt install cmake build-essential libssl-dev libsqlite3-dev libgit2-dev

# Python packages (optional, for WebSocket/MQTT/gRPC protocols)
pip install websocket-client paho-mqtt grpcio grpcio-tools
```

### 1. Build the C-Core

```bash
cd core
mkdir -p build_release && cd build_release
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

### 2. Start the Bridge Server

```bash
cd bridge
python3 python_bridge.py
```

The C-Core initializes its thread pool, mmap telemetry log, and IPC socket automatically on boot:

```
[C-Core Pool] Asynchronous Interceptor Grid deployed with 8 active threads!
[C-Core IPC] Pool Main Router listening at: /tmp/lightbase.sock 🎧
[Bridge] LightBase API → http://localhost:8000 🚀
```

### 3. Open the UI

Open `ui/index.html` in your browser, or serve it locally:

```bash
cd ui && python3 -m http.server 3000
# → http://localhost:3000
```

---

## 📂 Project Structure

```
LightBase/
├── core/                   # Native C engine
│   ├── include/            # Public headers (engine.h, storage.h, tlv.h)
│   ├── src/                # Source files
│   │   ├── engine.c        # HTTPS engine, IPC router, SQL executor
│   │   ├── storage.c       # mmap telemetry, schema scanner, env manager
│   │   ├── thread_pool.c   # 8-thread worker pool
│   │   ├── crypto_vault.c  # AES-256-GCM key encryption
│   │   ├── ai_core.c       # Local llama.cpp inference bridge
│   │   └── ...
│   └── build_release/      # Build artifacts (libcore.so)
├── bridge/
│   └── python_bridge.py    # HTTP gateway + plugin runner + export engine
├── ui/
│   ├── index.html          # Main application shell
│   ├── app.js              # Core logic (requests, env, tabs, tests)
│   ├── features.js         # Extended features (plugins, flows, AI, exports)
│   └── style.css           # Glassmorphism design system
├── workspace/              # 100% local, Git-versionable storage
│   ├── collections/        # Saved API request configs
│   ├── environments/       # Variable sets (dev/staging/prod)
│   ├── plugins/            # Python plugin scripts
│   ├── flows/              # Visual workflow definitions
│   ├── history/            # Auto-logged request history
│   ├── monitors/           # Scheduled run configs & results
│   ├── exports/            # Generated .ipynb and .py files
│   └── docs/               # Generated OpenAPI specs
├── docs/
│   └── user_guide.md       # Comprehensive user documentation
└── dist/                   # Production distribution bundle
```

---

## 📊 Performance

LightBase delivers sub-millisecond core processing by bypassing the network stack entirely:

| Operation | Latency |
|-----------|---------|
| Local SQLite query (via arena allocator) | **~750 µs** |
| IPC roundtrip (Python ↔ C-Core) | **~1.2 ms** |
| Outbound HTTPS (OpenSSL TLS 1.3) | Variable, with µs-precision `CLOCK_MONOTONIC` tracking |
| Telemetry write (mmap ring buffer) | **< 10 µs** |

---

## 📖 Documentation

For complete feature documentation, usage guides, and API reference:

**→ [User Guide](docs/user_guide.md)**

Covers all features including API Studio protocols, test scripts, visual flows, plugin system, AI workflows, exports, and CI/CD integration.

---

## 🛠️ CLI Usage

Run headless requests without the UI:

```bash
# Via the included CLI script
./bridge/lb-cli.sh httpbin.org /get GET

# Or directly via curl
curl -X POST http://localhost:8000/request \
  -H "Content-Type: application/json" \
  -d '{"method":"GET","hostname":"httpbin.org","path":"/get","headers":[]}'
```

---

## 📄 License

Licensed under the [Apache License 2.0](LICENSE).

---

*Built with ⚡ — 100% local, zero cloud dependencies.*
