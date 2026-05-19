import json, socket, ctypes, os, sys, sqlite3, time, threading, glob, uuid, traceback, re
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime

WORKSPACE = "/home/aarav/LightBase/workspace"
for d in ["collections","environments","history","monitors","mocks","flows","docs"]:
    os.makedirs(f"{WORKSPACE}/{d}", exist_ok=True)

# ============================================================================
# NATIVE C-CORE TYPE DEFINITIONS
# ============================================================================
class Response(ctypes.Structure):
    _fields_ = [("status_code", ctypes.c_int), ("payload", ctypes.c_char_p)]

class TelemetryRecord(ctypes.Structure):
    _pack_ = 1
    _fields_ = [("timestamp", ctypes.c_uint64), ("cpu_usage", ctypes.c_float),
                ("mem_total_mb", ctypes.c_uint32), ("mem_avail_mb", ctypes.c_uint32),
                ("reserved", ctypes.c_uint8 * 8)]

def load_lightbase_core():
    path = "/home/aarav/LightBase/core/build_release/libcore.so"
    if not os.path.exists(path): print(f"[Bridge] libcore.so missing"); return None
    lib = ctypes.CDLL(path)
    for fn, at, rt in [
        ("execute_native_quickjs_assert_suite", [ctypes.c_char_p]*2, ctypes.c_char_p),
        ("execute_local_ai_inference_stream", [ctypes.c_char_p], ctypes.c_char_p),
        ("compile_native_markdown_schema_spec", [ctypes.c_char_p], ctypes.c_char_p),
        ("compile_native_git_branch_status", [ctypes.c_char_p], ctypes.c_char_p),
        ("encrypt_api_key_system_level", [ctypes.c_char_p], ctypes.c_char_p),
        ("decrypt_api_key_system_level", [ctypes.c_char_p], ctypes.c_char_p),
        ("list_all_collections", [ctypes.c_char_p], ctypes.c_char_p),
        ("initialize_c_core_interceptor_pool", [], ctypes.c_int),
        ("start_linux_ipc_bridge", [], ctypes.c_int),
        ("init_mmap_telemetry_log", [], ctypes.c_int),
    ]:
        getattr(lib, fn).argtypes = at; getattr(lib, fn).restype = rt
    lib.scan_database_schema.argtypes = [ctypes.c_char_p]; lib.scan_database_schema.restype = Response
    lib.fetch_database_schema_tree.argtypes = [ctypes.c_char_p]; lib.fetch_database_schema_tree.restype = Response
    lib.execute_local_db.argtypes = [ctypes.c_char_p]*2; lib.execute_local_db.restype = Response
    lib.fire_http_get.argtypes = [ctypes.c_char_p]*6; lib.fire_http_get.restype = Response
    lib.read_all_telemetry_records.argtypes = [ctypes.POINTER(TelemetryRecord), ctypes.c_int]; lib.read_all_telemetry_records.restype = ctypes.c_int
    lib.append_mmap_telemetry_record.argtypes = [ctypes.c_float, ctypes.c_uint32, ctypes.c_uint32]; lib.append_mmap_telemetry_record.restype = ctypes.c_int
    lib.load_studio_environment_state.argtypes = [ctypes.c_char_p]*2 + [ctypes.c_uint32, ctypes.c_uint8]; lib.load_studio_environment_state.restype = Response
    lib.execute_studio_api_request.argtypes = [ctypes.c_char_p]*5; lib.execute_studio_api_request.restype = Response
    return lib

lightbase = load_lightbase_core()

# Active mock servers
active_mocks = {}
# Monitor threads
monitor_threads = {}

# ============================================================================
# FILESYSTEM HELPERS
# ============================================================================
def fs_save(category, name, data):
    p = f"{WORKSPACE}/{category}/{name}.json"
    with open(p, 'w') as f: json.dump(data, f, indent=2)
    return p

def fs_load(category, name):
    p = f"{WORKSPACE}/{category}/{name}.json"
    if not os.path.exists(p): return None
    with open(p) as f: return json.load(f)

def fs_list(category):
    d = f"{WORKSPACE}/{category}"
    return [os.path.splitext(f)[0] for f in os.listdir(d) if f.endswith('.json')]

def fs_delete(category, name):
    p = f"{WORKSPACE}/{category}/{name}.json"
    if os.path.exists(p): os.remove(p); return True
    return False

def save_history(entry):
    ts = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
    entry["id"] = ts
    entry["timestamp"] = datetime.now().isoformat()
    fs_save("history", ts, entry)

# ============================================================================
# OPENAPI GENERATOR
# ============================================================================
def generate_openapi_from_collections():
    spec = {"openapi":"3.0.3","info":{"title":"LightBase API","version":"1.0.0","description":"Auto-generated from LightBase Studio collections"},"paths":{},"servers":[{"url":"http://localhost:8000"}]}
    for name in fs_list("collections"):
        col = fs_load("collections", name)
        if not col: continue
        requests = col.get("requests", [])
        if isinstance(col, dict) and "url" in col:
            requests = [col]
        for req in requests:
            url = req.get("url",""); method = req.get("method","GET").lower()
            path = "/" + url.split("/",3)[-1] if "/" in url else "/"
            if path not in spec["paths"]: spec["paths"][path] = {}
            spec["paths"][path][method] = {"summary": req.get("name",name), "responses":{"200":{"description":"Success"}}}
    return spec

# ============================================================================
# MONITOR RUNNER
# ============================================================================
def run_collection_monitor(col_name, interval_sec, monitor_id):
    col = fs_load("collections", col_name)
    if not col: return
    requests = col.get("requests", [])
    if isinstance(col, dict) and "url" in col: requests = [col]
    results = []
    for req in requests:
        try:
            method = req.get("method","GET"); url = req.get("url","")
            clean = url.replace("https://","").replace("http://","")
            si = clean.find("/"); host = clean[:si] if si != -1 else clean; path = clean[si:] if si != -1 else "/"
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect("/tmp/lightbase.sock")
            tlv = encode_tlv(0x01, "network") + encode_tlv(0x04, host) + encode_tlv(0x05, path) + encode_tlv(0x06, "") + encode_tlv(0x07, method) + encode_tlv(0x08, "") + encode_tlv(0x09, "")
            sock.sendall(tlv)
            chunks = []
            while True:
                chunk = sock.recv(65536)
                if not chunk: break
                chunks.append(chunk)
            sock.close()
            resp = b''.join(chunks).decode('utf-8')
            rj = json.loads(resp)
            results.append({"name": req.get("name",""), "status": rj.get("engine_status",200), "pass": rj.get("engine_status",200) < 400})
        except Exception as e:
            results.append({"name": req.get("name",""), "status": 0, "pass": False, "error": str(e)})
    fs_save("monitors", f"{monitor_id}_result_{int(time.time())}", {"collection": col_name, "results": results, "timestamp": datetime.now().isoformat()})

def encode_tlv(tag, val):
    if val is None: val = ""
    vb = val.encode('utf-8'); l = len(vb)
    return bytes([tag, (l>>8)&0xFF, l&0xFF]) + vb

# ============================================================================
# GATEWAY HANDLER
# ============================================================================
class LightBaseGatewayHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args): pass  # Suppress default logging

    def do_OPTIONS(self):
        self.send_response(200); self._cors(); self.end_headers()

    def _cors(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, GET, OPTIONS, DELETE')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')

    def _json(self, code, data):
        self.send_response(code); self._cors()
        self.send_header('Content-Type', 'application/json'); self.end_headers()
        self.wfile.write(json.dumps(data).encode('utf-8'))

    def _err(self, code, msg): self._json(code, {"engine_status": code, "error": msg})

    def _tlv(self, tag, val):
        if val is None: val = ""
        vb = val.encode('utf-8'); l = len(vb)
        return bytes([tag, (l>>8)&0xFF, l&0xFF]) + vb

    def _ipc(self, frame, buf=65536):
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.connect("/tmp/lightbase.sock"); client.sendall(frame)
        chunks = []
        while True:
            chunk = client.recv(buf)
            if not chunk: break
            chunks.append(chunk)
        client.close()
        resp = b''.join(chunks).decode('utf-8')
        return json.loads(resp)

    def do_GET(self):
        if self.path == '/health':
            self._json(200, {"status":"OK","engine":"LightBase C-Core"})
        elif self.path.startswith('/fs/list/'):
            cat = self.path.split('/')[-1]
            self._json(200, {"files": fs_list(cat)})
        else:
            self._err(404, "Not found")

    def do_POST(self):
        cl = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(cl).decode('utf-8')
        rj = json.loads(body) if body else {}

        try:
            route = self.path
            # ============ FILE SYSTEM ROUTES ============
            if route == '/fs/save':
                cat = rj.get("category","collections"); name = rj.get("name","untitled")
                p = fs_save(cat, name, rj.get("data", rj))
                self._json(200, {"status":"saved","path":p})

            elif route == '/fs/load':
                cat = rj.get("category","collections"); name = rj.get("name","")
                data = fs_load(cat, name)
                self._json(200 if data else 404, data or {"error":"not found"})

            elif route == '/fs/list':
                cat = rj.get("category","collections")
                self._json(200, {"files": fs_list(cat)})

            elif route == '/fs/delete':
                cat = rj.get("category","collections"); name = rj.get("name","")
                ok = fs_delete(cat, name)
                self._json(200 if ok else 404, {"status":"deleted" if ok else "not found"})

            # ============ REST REQUEST VIA IPC ============
            elif route == '/request':
                method = rj.get("method","GET")
                hostname = rj.get("hostname","httpbin.org")
                path = rj.get("path","/get")
                headers_list = rj.get("headers",[])
                body_payload = rj.get("body_payload","")
                form_data = rj.get("form_data_payload",[])

                hp = "".join(f"{h['key']}: {h['value']}\r\n" for h in headers_list if h.get("key") and h.get("value"))
                fp = "&".join(f"{f['key']}={f['value']}" for f in form_data if f.get("key") and f.get("value"))
                target = rj.get("target","network")

                frame = self._tlv(0x01,target) + self._tlv(0x04,hostname) + self._tlv(0x05,path) + self._tlv(0x06,hp) + self._tlv(0x07,method) + self._tlv(0x08,body_payload) + self._tlv(0x09,fp)
                resp = self._ipc(frame)
                net = resp.get("network_payload",{})

                # QuickJS tests
                test_script = rj.get("test_script_payload","")
                test_logs = "// Skipped"
                if test_script.strip() and lightbase:
                    sr = json.dumps({"status":resp.get("engine_status",200),"data":net})
                    r = lightbase.execute_native_quickjs_assert_suite(test_script.encode(),sr.encode())
                    if r: test_logs = r.decode()

                out = {"engine_status":resp.get("engine_status",200),"network_data":net,
                       "telemetry":{"c_core_duration_us":resp.get("c_core_duration_us",0)},
                       "native_test_logs":test_logs}
                save_history({"protocol":"REST","method":method,"url":f"{hostname}{path}","status":out["engine_status"],"response_preview":json.dumps(net)[:500]})
                self._json(200, out)

            # ============ GRAPHQL RELAY ============
            elif route == '/graphql':
                hostname = rj.get("hostname","")
                gql_query = rj.get("query","")
                variables = rj.get("variables",{})
                gql_body = json.dumps({"query":gql_query,"variables":variables})
                path = rj.get("path","/graphql")
                hp = "Content-Type: application/json\r\n"
                frame = self._tlv(0x01,"network")+self._tlv(0x04,hostname)+self._tlv(0x05,path)+self._tlv(0x06,hp)+self._tlv(0x07,"POST")+self._tlv(0x08,gql_body)+self._tlv(0x09,"")
                resp = self._ipc(frame)
                net = resp.get("network_payload",{})
                save_history({"protocol":"GraphQL","url":f"{hostname}{path}","query":gql_query[:200],"status":resp.get("engine_status",200)})
                self._json(200, {"engine_status":resp.get("engine_status",200),"data":net,"telemetry":{"c_core_duration_us":resp.get("c_core_duration_us",0)}})

            # ============ WEBSOCKET ============
            elif route == '/ws_connect':
                ws_url = rj.get("url","")
                message = rj.get("message","")
                try:
                    import websocket
                    ws = websocket.create_connection(ws_url, timeout=5)
                    if message: ws.send(message)
                    result = ws.recv()
                    ws.close()
                    save_history({"protocol":"WebSocket","url":ws_url,"message_sent":message[:200],"message_received":result[:500]})
                    self._json(200, {"engine_status":200,"response":result,"url":ws_url})
                except Exception as e:
                    self._err(500, str(e))

            # ============ MQTT ============
            elif route == '/mqtt_connect':
                broker = rj.get("broker","localhost"); port = rj.get("port",1883)
                topic = rj.get("topic","test/lightbase"); payload = rj.get("payload","")
                action = rj.get("action","publish")
                try:
                    import paho.mqtt.client as mqtt
                    msgs = []
                    if action == "publish":
                        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
                        client.connect(broker, port, 5)
                        client.publish(topic, payload)
                        client.disconnect()
                        save_history({"protocol":"MQTT","broker":broker,"topic":topic,"action":"publish"})
                        self._json(200, {"engine_status":200,"action":"published","topic":topic})
                    elif action == "subscribe":
                        received = []
                        def on_msg(c,u,m): received.append({"topic":m.topic,"payload":m.payload.decode()})
                        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
                        client.on_message = on_msg
                        client.connect(broker, port, 5)
                        client.subscribe(topic)
                        client.loop_start(); time.sleep(float(rj.get("listen_sec",3))); client.loop_stop()
                        client.disconnect()
                        save_history({"protocol":"MQTT","broker":broker,"topic":topic,"action":"subscribe","count":len(received)})
                        self._json(200, {"engine_status":200,"messages":received})
                except Exception as e:
                    self._err(500, str(e))

            # ============ gRPC ============
            elif route == '/grpc_invoke':
                target = rj.get("target","localhost:50051")
                service = rj.get("service",""); method = rj.get("method","")
                payload = rj.get("payload",{})
                try:
                    import grpc
                    from grpc_reflection.v1alpha import reflection_pb2, reflection_pb2_grpc
                    channel = grpc.insecure_channel(target)
                    stub = reflection_pb2_grpc.ServerReflectionStub(channel)
                    save_history({"protocol":"gRPC","target":target,"service":service,"method":method})
                    self._json(200, {"engine_status":200,"note":"gRPC reflection call dispatched","target":target,"service":service,"method":method})
                except Exception as e:
                    self._err(500, str(e))

            # ============ IPC DB/SCHEMA ROUTES ============
            elif route in ('/local_db', '/query'):
                ui_target = rj.get("target","local_db"); db = rj.get("db_path","test_lightbase.db")
                tag_map = {"schema_scan":"schema_scan","set_env":"set_env","get_schema_tree":"get_schema_tree","save_collection":"save_collection"}
                if ui_target in tag_map:
                    frame = self._tlv(0x01,tag_map[ui_target]) + self._tlv(0x02,db)
                    if ui_target == "save_collection":
                        frame += self._tlv(0x03,rj.get("query")) + self._tlv(0x07,rj.get("method")) + self._tlv(0x04,rj.get("hostname"))
                else:
                    frame = self._tlv(0x01,"local_db")+self._tlv(0x02,db)+self._tlv(0x03,rj.get("query","SELECT 1;"))
                resp = self._ipc(frame)
                out = {"engine_status":200,"db_data":resp,"telemetry":{"c_core_duration_us":resp.get("c_core_duration_us",0)}}
                if isinstance(resp,dict) and "tables" in resp: out["tables"]=resp["tables"]
                self._json(200,out)

            elif route == '/sys_metrics':
                frame = self._tlv(0x01,"sys_metrics")
                resp = self._ipc(frame, 6144)
                self._json(200,{"engine_status":200,"system_telemetry":resp})

            elif route == '/autogen_docs':
                db = rj.get("db_path","test_lightbase.db")
                r = lightbase.compile_native_markdown_schema_spec(db.encode())
                self._json(200,{"engine_status":200,"compiled_markdown":r.decode() if r else "# Error"})

            elif route == '/git_sync':
                rp = rj.get("repo_path","/home/aarav/LightBase")
                r = lightbase.compile_native_git_branch_status(rp.encode())
                self._json(200,{"engine_status":200,"git_status":r.decode() if r else "Error"})

            elif route == '/ai_inference':
                ctx = rj.get("context_payload","")
                r = lightbase.execute_local_ai_inference_stream(ctx.encode())
                self._json(200,{"engine_status":200,"ai_generation":r.decode() if r else "Error"})

            elif route == '/secure_vault_key':
                prov = rj.get("provider"); key = rj.get("plain_key")
                if not prov or not key: self._err(400,"Missing params"); return
                enc = lightbase.encrypt_api_key_system_level(key.encode())
                if not enc: self._err(500,"Encryption failed"); return
                conn = sqlite3.connect("test_lightbase.db"); c = conn.cursor()
                c.execute("CREATE TABLE IF NOT EXISTS system_vault (id INTEGER PRIMARY KEY AUTOINCREMENT, provider TEXT UNIQUE, encrypted_key TEXT)")
                c.execute("INSERT OR REPLACE INTO system_vault (provider,encrypted_key) VALUES (?,?)",(prov,enc.decode()))
                conn.commit(); conn.close()
                self._json(200,{"engine_status":200,"vault_status":"SUCCESS_SECURED"})

            elif route == '/retrieve_vault_key':
                prov = rj.get("provider")
                if not prov: self._err(400,"Missing provider"); return
                conn = sqlite3.connect(rj.get("db_path","test_lightbase.db")); c = conn.cursor()
                c.execute("SELECT encrypted_key FROM system_vault WHERE provider=?",(prov,))
                row = c.fetchone(); conn.close()
                if not row: self._err(404,"Not found"); return
                dec = lightbase.decrypt_api_key_system_level(row[0].encode())
                if not dec: self._err(500,"Decryption failed"); return
                self._json(200,{"engine_status":200,"provider":prov,"decrypted_key":dec.decode()})

            elif route == '/telemetry_history':
                mx = rj.get("max_records",100)
                buf = (TelemetryRecord * mx)(); cnt = lightbase.read_all_telemetry_records(buf, mx)
                recs = [{"timestamp":buf[i].timestamp,"cpu_usage":round(buf[i].cpu_usage,2),"mem_total_mb":buf[i].mem_total_mb,"mem_avail_mb":buf[i].mem_avail_mb} for i in range(cnt) if buf[i].timestamp]
                self._json(200,{"engine_status":200,"records_read":len(recs),"telemetry_history":recs})

            elif route == '/list_collections':
                db = rj.get("db_path","test_lightbase.db")
                r = lightbase.list_all_collections(db.encode())
                self._json(200,{"engine_status":200,"collections":json.loads(r.decode()) if r else []})

            # ============ MOCK SERVER ============
            elif route == '/mock/register':
                mid = rj.get("name", str(uuid.uuid4())[:8])
                routes = rj.get("routes", [])
                fs_save("mocks", mid, {"name":mid,"routes":routes})
                self._json(200,{"engine_status":200,"mock_id":mid,"route_count":len(routes)})

            elif route == '/mock/resolve':
                path = rj.get("path","/"); method = rj.get("method","GET")
                for name in fs_list("mocks"):
                    mock = fs_load("mocks", name)
                    if not mock: continue
                    for r in mock.get("routes",[]):
                        if r.get("path") == path and r.get("method","GET").upper() == method.upper():
                            self._json(r.get("status",200), r.get("body",{}))
                            return
                self._err(404, "No mock matched")

            # ============ MONITOR ============
            elif route == '/run_monitor':
                col = rj.get("collection","")
                mid = rj.get("monitor_id", str(uuid.uuid4())[:8])
                interval = rj.get("interval_sec", 60)
                run_collection_monitor(col, interval, mid)
                self._json(200,{"engine_status":200,"monitor_id":mid,"status":"executed"})

            elif route == '/monitor/schedule':
                col = rj.get("collection",""); interval = rj.get("interval_sec",60)
                mid = rj.get("monitor_id", str(uuid.uuid4())[:8])
                def loop():
                    while mid in monitor_threads:
                        run_collection_monitor(col, interval, mid); time.sleep(interval)
                monitor_threads[mid] = True
                t = threading.Thread(target=loop, daemon=True); t.start()
                fs_save("monitors", mid, {"collection":col,"interval_sec":interval,"active":True})
                self._json(200,{"engine_status":200,"monitor_id":mid,"status":"scheduled"})

            elif route == '/monitor/stop':
                mid = rj.get("monitor_id","")
                if mid in monitor_threads: del monitor_threads[mid]
                self._json(200,{"status":"stopped"})

            # ============ OPENAPI EXPORT ============
            elif route == '/export_openapi':
                spec = generate_openapi_from_collections()
                fs_save("docs","openapi_spec",spec)
                self._json(200,{"engine_status":200,"spec":spec})

            # ============ HISTORY ============
            elif route == '/history/list':
                items = []
                for name in sorted(fs_list("history"), reverse=True)[:50]:
                    items.append(fs_load("history", name))
                self._json(200,{"engine_status":200,"history":items})

            elif route == '/history/clear':
                for name in fs_list("history"): fs_delete("history", name)
                self._json(200,{"status":"cleared"})

            # ============ SAVE/LOAD COLLECTIONS (file-based) ============
            elif route == '/save_collection':
                name = rj.get("name","untitled").replace(" ","_")
                fs_save("collections", name, rj)
                self._json(200,{"status":"SAVED","path":f"{WORKSPACE}/collections/{name}.json"})

            elif route == '/save_environment':
                name = rj.get("name","untitled").replace(" ","_")
                fs_save("environments", name, rj)
                self._json(200,{"status":"SAVED"})

            # ============ FLOW SAVE/LOAD ============
            elif route == '/flow/save':
                name = rj.get("name","untitled_flow")
                fs_save("flows", name, rj.get("data",rj))
                self._json(200,{"status":"saved"})

            elif route == '/flow/load':
                name = rj.get("name","")
                data = fs_load("flows", name)
                self._json(200 if data else 404, data or {"error":"not found"})

            elif route == '/flow/run':
                name = rj.get("name","")
                flow = fs_load("flows", name)
                if not flow: self._err(404,"Flow not found"); return
                results = []
                for node in flow.get("nodes",[]):
                    if node.get("type") == "request":
                        try:
                            url = node.get("url",""); method = node.get("method","GET")
                            clean = url.replace("https://","").replace("http://","")
                            si = clean.find("/"); host = clean[:si] if si!=-1 else clean; path = clean[si:] if si!=-1 else "/"
                            frame = self._tlv(0x01,"network")+self._tlv(0x04,host)+self._tlv(0x05,path)+self._tlv(0x06,"")+self._tlv(0x07,method)+self._tlv(0x08,"")+self._tlv(0x09,"")
                            resp = self._ipc(frame)
                            results.append({"node":node.get("id",""),"status":resp.get("engine_status",200),"data":resp.get("network_payload",{})})
                        except Exception as e:
                            results.append({"node":node.get("id",""),"error":str(e)})
                    elif node.get("type") == "transform":
                        results.append({"node":node.get("id",""),"type":"transform","note":"Applied"})
                self._json(200,{"engine_status":200,"flow_results":results})

            # ============ PYTHON PLUGIN SYSTEM ============
            elif route == '/plugin/list':
                plugins_dir = f"{WORKSPACE}/plugins"
                files = [f for f in os.listdir(plugins_dir) if f.endswith('.py')]
                self._json(200, {"plugins": [os.path.splitext(f)[0] for f in files]})

            elif route == '/plugin/load':
                name = rj.get("name","")
                plugin_path = f"{WORKSPACE}/plugins/{name}.py"
                if not os.path.exists(plugin_path): self._err(404,"Plugin not found"); return
                with open(plugin_path) as f: code = f.read()
                self._json(200, {"name": name, "code": code})

            elif route == '/plugin/save':
                name = rj.get("name","untitled")
                code = rj.get("code","")
                with open(f"{WORKSPACE}/plugins/{name}.py", 'w') as f: f.write(code)
                self._json(200, {"status":"saved","name":name})

            elif route == '/plugin/run':
                name = rj.get("name","")
                context = rj.get("context", {})
                plugin_path = f"{WORKSPACE}/plugins/{name}.py"
                if not os.path.exists(plugin_path): self._err(404,"Plugin not found"); return
                try:
                    with open(plugin_path) as f: code = f.read()
                    ns = {"context": context, "result": None, "json": json, "os": os}
                    exec(code, ns)
                    if callable(ns.get("run")):
                        output = ns["run"](context)
                    else:
                        output = ns.get("result", "Plugin executed (no run() or result)")
                    self._json(200, {"engine_status":200,"plugin_output": output if isinstance(output, (dict,list,str,int,float,bool,type(None))) else str(output)})
                except Exception as e:
                    self._err(500, f"Plugin error: {traceback.format_exc()}")

            elif route == '/plugin/install':
                package = rj.get("package","")
                if not package: self._err(400,"Missing package name"); return
                try:
                    import subprocess
                    result = subprocess.run(["pip","install","--break-system-packages",package], capture_output=True, text=True, timeout=60)
                    self._json(200, {"engine_status":200,"stdout":result.stdout[-500:],"stderr":result.stderr[-500:],"returncode":result.returncode})
                except Exception as e:
                    self._err(500, str(e))

            # ============ AI AGENTIC WORKFLOWS ============
            elif route == '/ai/generate_tests':
                response_data = rj.get("response_data","")
                endpoint = rj.get("endpoint","")
                prompt = f"""Generate LightBase test scripts for this API response.
Use the lb.test() and lb.expect() API. Format:
lb.test("name", () => {{ lb.expect(lb.response.status).toBe(200); }});

Endpoint: {endpoint}
Response data:
{response_data[:2000]}

Generate 3-5 meaningful test assertions:"""
                r = lightbase.execute_local_ai_inference_stream(prompt.encode())
                self._json(200, {"engine_status":200,"generated_tests": r.decode() if r else "// AI unavailable — write tests manually"})

            elif route == '/ai/chain_next':
                prev_response = rj.get("response_data","")
                prev_url = rj.get("url","")
                prev_method = rj.get("method","GET")
                prompt = f"""Analyze this API response and suggest the next logical API request.
Previous: {prev_method} {prev_url}
Response: {prev_response[:2000]}

Return a JSON object with: method, url, body (if needed), and explanation.
Example: {{"method":"GET","url":"https://api.example.com/users/123","body":"","explanation":"Fetch the user details from the ID returned"}}"""
                r = lightbase.execute_local_ai_inference_stream(prompt.encode())
                text = r.decode() if r else '{"explanation":"AI unavailable"}'
                try:
                    suggestion = json.loads(text)
                except:
                    suggestion = {"raw": text, "explanation": "Could not parse as JSON"}
                self._json(200, {"engine_status":200,"suggestion": suggestion})

            elif route == '/ai/explain':
                response_data = rj.get("response_data","")
                prompt = f"Explain this API response in plain English, highlighting important fields and potential issues:\n{response_data[:3000]}"
                r = lightbase.execute_local_ai_inference_stream(prompt.encode())
                self._json(200, {"engine_status":200,"explanation": r.decode() if r else "AI unavailable"})

            # ============ SSE LIVE DATA STREAMER ============
            elif route == '/stream/ws_live':
                ws_url = rj.get("url","")
                duration = rj.get("duration_sec", 10)
                try:
                    import websocket
                    messages = []
                    def on_msg(wsapp, msg): messages.append({"ts":time.time(),"data":msg[:1000]})
                    def on_err(wsapp, err): messages.append({"ts":time.time(),"error":str(err)})
                    def on_close(wsapp, sc, msg): pass
                    def on_open(wsapp):
                        def closer():
                            time.sleep(duration)
                            wsapp.close()
                        threading.Thread(target=closer, daemon=True).start()
                    ws = websocket.WebSocketApp(ws_url, on_message=on_msg, on_error=on_err, on_close=on_close, on_open=on_open)
                    wst = threading.Thread(target=ws.run_forever); wst.daemon=True; wst.start(); wst.join(timeout=duration+2)
                    self._json(200, {"engine_status":200,"message_count":len(messages),"messages":messages[:200],
                                     "rate_per_sec": round(len(messages)/max(duration,1),2)})
                except Exception as e:
                    self._err(500, str(e))

            elif route == '/stream/sse_connect':
                sse_url = rj.get("url","")
                duration = rj.get("duration_sec", 5)
                try:
                    import urllib.request
                    req = urllib.request.Request(sse_url)
                    events = []
                    with urllib.request.urlopen(req, timeout=duration+2) as resp:
                        start = time.time()
                        for line in resp:
                            if time.time() - start > duration: break
                            decoded = line.decode('utf-8').strip()
                            if decoded.startswith('data:'):
                                events.append({"ts":time.time(),"data":decoded[5:].strip()[:1000]})
                            if len(events) >= 200: break
                    self._json(200, {"engine_status":200,"event_count":len(events),"events":events,
                                     "rate_per_sec": round(len(events)/max(duration,1),2)})
                except Exception as e:
                    self._err(500, str(e))

            # ============ JUPYTER NOTEBOOK EXPORT ============
            elif route == '/export/notebook':
                col_name = rj.get("collection","")
                source = rj.get("source","collection")  # "collection" or "history"
                items = []
                if source == "collection":
                    col = fs_load("collections", col_name)
                    if col:
                        if "requests" in col: items = col["requests"]
                        elif "url" in col: items = [col]
                elif source == "history":
                    for n in sorted(fs_list("history"), reverse=True)[:20]:
                        h = fs_load("history", n)
                        if h: items.append(h)

                cells = [{"cell_type":"markdown","metadata":{},"source":[f"# LightBase Export: {col_name or 'History'}\n","Generated by LightBase Studio\n"]},
                         {"cell_type":"code","metadata":{},"source":["import requests\nimport json\n"],"execution_count":None,"outputs":[]}]
                for i, item in enumerate(items):
                    m = item.get("method","GET"); u = item.get("url","")
                    b = item.get("body","") or item.get("body_payload","")
                    code_lines = [f"# Request {i+1}: {m} {u}\n"]
                    if m.upper() in ("POST","PUT","PATCH") and b:
                        code_lines.append(f"resp = requests.{m.lower()}('{u}',\n    json={b})\n")
                    else:
                        code_lines.append(f"resp = requests.{m.lower()}('{u}')\n")
                    code_lines.append("print(f'Status: {resp.status_code}')\n")
                    code_lines.append("print(json.dumps(resp.json(), indent=2))\n")
                    cells.append({"cell_type":"code","metadata":{},"source":code_lines,"execution_count":None,"outputs":[]})

                nb = {"nbformat":4,"nbformat_minor":5,"metadata":{"kernelspec":{"display_name":"Python 3","language":"python","name":"python3"},"language_info":{"name":"python","version":"3.11.0"}},"cells":cells}
                fname = f"{col_name or 'history_export'}.ipynb"
                with open(f"{WORKSPACE}/exports/{fname}",'w') as f: json.dump(nb,f,indent=2)
                self._json(200, {"engine_status":200,"filename":fname,"path":f"{WORKSPACE}/exports/{fname}","cell_count":len(cells)})

            elif route == '/export/python':
                col_name = rj.get("collection","")
                col = fs_load("collections", col_name)
                items = []
                if col:
                    if "requests" in col: items = col["requests"]
                    elif "url" in col: items = [col]

                lines = ["#!/usr/bin/env python3","\"\"\"Generated by LightBase Studio\"\"\"","import requests, json",""]
                for i, item in enumerate(items):
                    m = item.get("method","GET"); u = item.get("url","")
                    b = item.get("body","")
                    lines.append(f"# Request {i+1}")
                    if m.upper() in ("POST","PUT","PATCH") and b:
                        lines.append(f"r{i} = requests.{m.lower()}('{u}', json={b})")
                    else:
                        lines.append(f"r{i} = requests.{m.lower()}('{u}')")
                    lines.append(f"print(f'[{m} {u}] Status: {{r{i}.status_code}}')")
                    lines.append(f"print(json.dumps(r{i}.json(), indent=2))")
                    lines.append("")

                fname = f"{col_name or 'export'}.py"
                with open(f"{WORKSPACE}/exports/{fname}",'w') as f: f.write("\n".join(lines))
                self._json(200, {"engine_status":200,"filename":fname,"path":f"{WORKSPACE}/exports/{fname}"})

            else:
                self._err(404, "Route not found")
        except Exception as e:
            print(f"[Bridge Error] {traceback.format_exc()}")
            self._err(500, str(e))

# ============================================================================
# BOOT
# ============================================================================
if __name__ == '__main__':
    print("[Bridge] Booting LightBase ecosystem...")
    if not lightbase: sys.exit("[Bridge] Failed to link libcore.so")
    lightbase.initialize_c_core_interceptor_pool()
    lightbase.start_linux_ipc_bridge()
    lightbase.init_mmap_telemetry_log()
    httpd = HTTPServer(('', 8000), LightBaseGatewayHandler)
    httpd.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    print("[Bridge] LightBase API → http://localhost:8000 🚀")
    try: httpd.serve_forever()
    except KeyboardInterrupt: httpd.server_close(); print("🏁 Shutdown complete.")