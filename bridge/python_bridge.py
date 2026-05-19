import json
import socket
import ctypes
from http.server import HTTPServer, BaseHTTPRequestHandler

# 1. DEFINE COMPACT PYTHON INTERFACE TYPES MATCHING C STRUCTS
class CResponse(ctypes.Structure):
    _fields_ = [
        ("status_code", ctypes.c_int),
        ("payload", ctypes.c_char_p)
    ]

def load_lightbase_core():
    lib_path = "/home/aarav/LightBase/dist/lib/libcore.so"
    core = ctypes.CDLL(lib_path)

    # Track persistent background thread IPC loop controller configuration
    core.start_linux_ipc_bridge.argtypes = []
    core.start_linux_ipc_bridge.restype = ctypes.c_int
    return core

# 2. THE API GATEWAY ROUTER (STRICT INDENTATION COMPLIANCE)
class LightBaseGatewayHandler(BaseHTTPRequestHandler):

    def do_OPTIONS(self):
        # Handle CORS safety checks seamlessly for browser clients
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def _set_cors_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')

    # Helper routine to serialize fields into formal Type-Length-Value byte strings
    def encode_tlv_field(self, tag, value_str):
        value_bytes = value_str.encode('utf-8')
        length = len(value_bytes)
        # Format layout: Tag (1B), Length (2B Big-Endian), Value (NB)
        header = bytes([tag, (length >> 8) & 0xFF, length & 0xFF])
        return header + value_bytes

    # --- ROUTE A: SECURE HTTPS NETWORK ENGINE OVER IPC ---
    def do_POST(self):
        # --- UPDATE PATH: SECURE HTTPS NETWORK ENGINE WITH CUSTOM HEADERS ---
        if self.path == '/request':
            content_length = int(self.headers.get('Content-Length', 0))
            post_data = self.rfile.read(content_length).decode('utf-8')
            req_json = json.loads(post_data)

            method = req_json.get("method", "GET")
            hostname = req_json.get("hostname", "jsonplaceholder.typicode.com")
            path = req_json.get("path", "/posts/1")

            print(f"[Python Gateway] Marshaling secure {method} request parameters for: {hostname}")

            # Extract custom headers array sent by the browser grid view
            custom_headers = req_json.get("headers", []) # Format: [{"key": "X-App", "value": "LightBase"}]
            body_payload = req_json.get("body_payload", "") # Extract request body payload text

            # Format custom headers into a single flat text stream line block
            headers_payload = ""
            for h in custom_headers:
                if h.get("key") and h.get("value"):
                    headers_payload += f"{h['key']}: {h['value']}\r\n"

            # Serialize fields into our sequential binary frame stream block
            # Tag 0x01=Type, Tag 0x04=Host, Tag 0x05=Path, Tag 0x06=Headers, Tag 0x07=Method, Tag 0x08=Body Content!
            tlv_frame = (
                    self.encode_tlv_field(0x01, "network") +
                    self.encode_tlv_field(0x04, hostname) +
                    self.encode_tlv_field(0x05, path) +
                    self.encode_tlv_field(0x06, headers_payload) +
                    self.encode_tlv_field(0x07, method) +
                    self.encode_tlv_field(0x08, body_payload)
            )

            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.connect("/tmp/lightbase.sock")
            client.sendall(tlv_frame)

            raw_response = client.recv(65536).decode('utf-8') # Increase read buffer descriptor for large sets
            client.close()

            response_json = json.loads(raw_response)

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self._set_cors_headers()
            self.end_headers()

            output = {
                "engine_status": 200,
                "network_data": response_json.get("network_payload", {}),
                "telemetry": { "c_core_duration_us": response_json.get("c_core_duration_us", 0) }
            }
            self.wfile.write(json.dumps(output).encode('utf-8'))

        # --- ROUTE B: ARENA-POWERED DATABASE ROUTE OVER IPC ---
        elif self.path == '/local_db' or self.path == '/query':
            content_length = int(self.headers['Content-Length'])
            post_data = self.rfile.read(content_length).decode('utf-8')
            req_json = json.loads(post_data)

            # Detect whether frontend is calling a normal raw SQL string or an explicit metadata sidebar scan
            ui_target = req_json.get("target", "local_db")
            db_path = req_json.get("db_path", "test_lightbase.db")

            if ui_target == "schema_scan":
                tlv_frame = (
                        self.encode_tlv_field(0x01, "schema_scan") +
                        self.encode_tlv_field(0x02, db_path)
                )
            elif ui_target == "set_env":
                # Match binary layout markers: Tag 0x01 = set_env, Tag 0x02 = Target Active DB Path
                env_name = req_json.get("env_name", "Development")
                tlv_frame = (
                        self.encode_tlv_field(0x01, "set_env") +
                        self.encode_tlv_field(0x02, db_path)
                )
            else:
                raw_query = req_json.get("query", "SELECT * FROM users;")
                tlv_frame = (
                        self.encode_tlv_field(0x01, "local_db") +
                        self.encode_tlv_field(0x02, db_path) +
                        self.encode_tlv_field(0x03, raw_query)
                )

            # (Socket transmission code blocks remain completely un-compromised)
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.connect("/tmp/lightbase.sock")
            client.sendall(tlv_frame)

            raw_response = client.recv(16384).decode('utf-8')
            client.close()

            response_json = json.loads(raw_response)

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()

            output = {
                "engine_status": 200,
                "db_data": response_json, # Passes parsed tables/views object arrays straight to UI grid trees
                "telemetry": { "c_core_duration_us": response_json.get("c_core_duration_us", 0) }
            }
            self.wfile.write(json.dumps(output).encode('utf-8'))

        # --- ROUTE C: REAL-TIME LINUX KERNEL TELEMETRY METRICS ROUTE ---
        elif self.path == '/sys_metrics':
            tlv_frame = self.encode_tlv_field(0x01, "sys_metrics")

            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.connect("/tmp/lightbase.sock")
            client.sendall(tlv_frame)

            raw_response = client.recv(6144).decode('utf-8')
            client.close()

            response_json = json.loads(raw_response)

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()

            output = { "engine_status": 200, "system_telemetry": response_json }
            self.wfile.write(json.dumps(output).encode('utf-8'))




# 3. SERVICE RUNTIME DEPLOYMENT CHECKPOINT
if __name__ == '__main__':
    print("[Python Gateway] Booting LightBase ecosystem runtime core setup...")
    lightbase = load_lightbase_core()

    # Spawns our multi-threaded POSIX background worker routing daemon in C
    status = lightbase.start_linux_ipc_bridge()
    print(f"[Python Gateway] C-Core background thread initialized. Code: {status}")

    server_address = ('', 8000)
    httpd = HTTPServer(server_address, LightBaseGatewayHandler)
    print("[Python Server] LightBase API serving at http://localhost:8000 🚀")
    httpd.serve_forever()