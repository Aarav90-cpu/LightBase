import ctypes
import os
import sys
import json
from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn

class CResponse(ctypes.Structure):
    _fields_ = [
        ("status_code", ctypes.c_int),
        ("payload", ctypes.c_char_p)
    ]

def load_lightbase_core():
    # Update target route to link directly against our optimized distribution asset directory
    lib_path = "/home/aarav/LightBase/dist/lib/libcore.so"

    core = ctypes.CDLL(lib_path)
    core.execute_raw_query.argtypes = [ctypes.c_char_p]
    core.execute_raw_query.restype = CResponse
    core.fire_http_get.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    core.fire_http_get.restype = CResponse
    core.execute_local_db.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
    core.execute_local_db.restype = CResponse

    core.start_linux_ipc_bridge.argtypes = []
    core.start_linux_ipc_bridge.restype = ctypes.c_int
    return core

lightbase = load_lightbase_core()

class LightBaseAPIHandler(BaseHTTPRequestHandler):

    def _send_cors_and_headers(self, status_code=200):
        """Sends the HTTP response code and all mandatory headers cleanly."""
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_OPTIONS(self):
        """Handles browser CORS preflight checks smoothly."""
        self._send_cors_and_headers(200)

    def do_POST(self):
        """Processes downstream operations for the LightBase stack."""
        content_length = int(self.headers.get('Content-Length', 0))
        post_data = self.rfile.read(content_length)

        try:
            request_json = json.loads(post_data.decode('utf-8'))
        except Exception:
            self._send_cors_and_headers(400)
            self.wfile.write(json.dumps({"error": "Invalid JSON payload"}).encode('utf-8'))
            return

        # ROUTE 1: Local Mock Query
        if self.path == "/query":
            raw_query = request_json.get("query", "")
            c_response = lightbase.execute_raw_query(raw_query.encode('utf-8'))
            self._send_cors_and_headers(200)
            response_payload = {
                "engine_status": c_response.status_code,
                "data": json.loads(c_response.payload.decode('utf-8'))
            }
            self.wfile.write(json.dumps(response_payload).encode('utf-8'))

        # ROUTE 2: Outbound Network Request Engine via Native Unix Domain Sockets!
        elif self.path == "/request":
            import socket as py_socket
            import time
            py_start = time.perf_counter()

            hostname = request_json.get("hostname", "")
            path = request_json.get("path", "")

            # Pack the network request parameters into our IPC target packet layout
            ipc_payload = json.dumps({
                "target": "network",
                "hostname": hostname,
                "path": path
            })

            try:
                client = py_socket.socket(py_socket.AF_UNIX, py_socket.SOCK_STREAM)
                client.connect("/tmp/lightbase.sock")
                client.sendall(ipc_payload.encode('utf-8'))

                chunks = []
                while True:
                    chunk = client.recv(4096)
                    if not chunk:
                        break
                    chunks.append(chunk)

                c_ipc_response = b"".join(chunks).decode('utf-8')
                client.close()

                c_unpacked_payload = json.loads(c_ipc_response)
                engine_status = 200
            except Exception as e:
                c_unpacked_payload = {"error": f"Network IPC channel transport tracking fault: {e}"}
                engine_status = 500

            py_duration_ms = (time.perf_counter() - py_start) * 1000
            self._send_cors_and_headers(engine_status)

            response_payload = {
                "engine_status": engine_status,
                "network_data": c_unpacked_payload.get("network_payload", c_unpacked_payload),
                "telemetry": {
                    "c_core_duration_us": c_unpacked_payload.get("c_core_duration_us", 0.0),
                    "total_ipc_roundtrip_latency_ms": round(py_duration_ms, 3)
                }
            }
            self.wfile.write(json.dumps(response_payload).encode('utf-8'))

        elif self.path == "/local_db":
            import socket as py_socket
            import time
            py_start = time.perf_counter()

            db_path = request_json.get("db_path", "")
            sql_query = request_json.get("query", "")

            # Construct a unified payload string tracking our target signature match
            ipc_payload = json.dumps({
                "target": "local_db",
                "db_path": db_path,
                "query": sql_query
            })

            try:
                # Open a direct, high-speed connection stream to the running C background thread
                client = py_socket.socket(py_socket.AF_UNIX, py_socket.SOCK_STREAM)
                client.connect("/tmp/lightbase.sock")

                # Blast the raw characters down the kernel node channel
                client.sendall(ipc_payload.encode('utf-8'))

                # Collect the returned string modifications from the C heap allocations
                chunks = []
                while True:
                    chunk = client.recv(4096)
                    if not chunk:
                        break
                    chunks.append(chunk)

                c_ipc_response = b"".join(chunks).decode('utf-8')
                client.close()

                c_unpacked_payload = json.loads(c_ipc_response)
                engine_status = 200
            except Exception as e:
                c_unpacked_payload = {"error": f"IPC transport channel tracking fault: {e}"}
                engine_status = 500

            py_duration_ms = (time.perf_counter() - py_start) * 1000
            self._send_cors_and_headers(engine_status)

            response_payload = {
                "engine_status": engine_status,
                "db_data": c_unpacked_payload.get("rows", c_unpacked_payload),
                "telemetry": {
                    "c_core_duration_us": c_unpacked_payload.get("c_core_duration_us", 0.0),
                    "total_ipc_roundtrip_latency_ms": round(py_duration_ms, 3)
                }
            }
            self.wfile.write(json.dumps(response_payload).encode('utf-8'))

class ThreadedHTTPServer(ThreadingMixIn, HTTPServer):
    pass

if __name__ == "__main__":
    import socket as py_socket
    import time

    # 1. Fire up the C background worker thread infrastructure
    ipc_status = lightbase.start_linux_ipc_bridge()
    print(f"[Python Gateway] C-Core background thread initialized. Code: {ipc_status}")

    # Give the C thread a quick 50ms window to bind the filesystem node safely
    time.sleep(0.05)

    # 2. Open an independent client socket file stream targeting the dynamic file
    try:
        client = py_socket.socket(py_socket.AF_UNIX, py_socket.SOCK_STREAM)
        client.connect("/tmp/lightbase.sock")

        test_payload = "{\"command\": \"ping\", \"origin\": \"python_runtime\"}"
        client.sendall(test_payload.encode('utf-8'))

        raw_response = client.recv(1024)
        print(f"[Python Gateway UDS Mirror Response]: {raw_response.decode('utf-8')}")
        client.close()
    except Exception as e:
        print(f"[Python Gateway UDS Error Log]: Failed to map connection layout link. Details: {e}")

    # 3. Boot up the standard external web interface loop cleanly
    server_address = ('localhost', 8000)
    httpd = ThreadedHTTPServer(server_address, LightBaseAPIHandler)
    print("[Python Server] LightBase API serving at http://localhost:8000 🚀")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[Python Server] Shutting down cleanly.")
        httpd.server_close()