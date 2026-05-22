// LightBase Studio — Core Application Logic
const API = "http://localhost:8000";
const cpuHist = []; const MAX_H = 30;
let curEnv = 'dev', method = 'GET', respCache = '';
const cookies = {}, mocks = {};
const envVars = {
    dev: {baseUrl:'httpbin.org',authToken:'Bearer dev_123',timeout:'5000'},
    staging: {baseUrl:'staging.httpbin.org',authToken:'Bearer stg_abc',timeout:'8000'},
    prod: {baseUrl:'httpbin.org',authToken:'Bearer prod_xyz',timeout:'10000'}
};
let flowNodes = [], flowIdCounter = 0;

// === UTILS ===
const $ = id => document.getElementById(id);
const getDb = () => curEnv==='prod'?'prod_lightbase.sovereign.db':curEnv==='staging'?'staging_lightbase.db':'test_lightbase.db';
function resolve(s) { if(!s)return s; const t=envVars[curEnv]; return s.replace(/\{\{([^}]+)\}\}/g,(_,k)=>t&&t[k.trim()]||_); }
function hexDump(s){if(!s)return'';const b=new TextEncoder().encode(s);let o='';for(let i=0;i<b.length;i+=16){let h='',a='';for(let j=0;j<16;j++){if(i+j<b.length){const c=b[i+j];h+=c.toString(16).padStart(2,'0')+' ';a+=(c>=32&&c<=126)?String.fromCharCode(c):'.';}else h+='   ';if(j===7)h+=' ';}o+=i.toString(16).padStart(8,'0')+'  '+h.padEnd(49)+'  |'+a+'|\n';}return o;}

// === TABS ===
function switchTab(id,el){document.querySelectorAll('.tab-content').forEach(t=>t.classList.remove('active'));document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));$('tab-'+id).classList.add('active');el.classList.add('active');}
function switchProto(id,el){document.querySelectorAll('.proto-panel').forEach(p=>p.classList.remove('active'));document.querySelectorAll('.proto-tab').forEach(t=>t.classList.remove('active'));$('proto-'+id).classList.add('active');el.classList.add('active');}
$('sidebar-toggle').addEventListener('click',()=>$('sidebar').classList.toggle('collapsed'));

// === ENV ===
function switchEnv(){curEnv=$('env-selector').value;$('env-db-label').innerText=getDb();refreshSchemaTree();loadCollections();}
function openEnvEditor(){$('env-modal').classList.remove('hidden');const ed=$('env-vars-editor');ed.innerHTML='';const v=envVars[curEnv]||{};Object.keys(v).forEach(k=>{const r=document.createElement('div');r.className='kv-row';r.innerHTML=`<input class="kv-key" value="${k}"><input class="kv-val" value="${v[k]}"><button class="kv-remove" onclick="this.parentElement.remove()">✕</button>`;ed.appendChild(r);});}
function closeEnvEditor(){$('env-modal').classList.add('hidden');}
function addEnvVar(){const r=document.createElement('div');r.className='kv-row';r.innerHTML='<input class="kv-key" placeholder="key"><input class="kv-val" placeholder="value"><button class="kv-remove" onclick="this.parentElement.remove()">✕</button>';$('env-vars-editor').appendChild(r);}
function saveEnvVars(){const v={};$('env-vars-editor').querySelectorAll('.kv-row').forEach(r=>{const k=r.querySelector('.kv-key').value.trim(),val=r.querySelector('.kv-val').value.trim();if(k)v[k]=val;});envVars[curEnv]=v;fetch(`${API}/save_environment`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:curEnv,variables:v})});closeEnvEditor();}

// === METHOD ===
function setMethod(m,el){method=m;document.querySelectorAll('.method-btn').forEach(b=>b.classList.remove('active'));el.classList.add('active');}

// === KV HELPERS ===
function addKv(id){const r=document.createElement('div');r.className='kv-row';r.innerHTML='<input class="kv-key" placeholder="Key"><input class="kv-val" placeholder="Value"><button class="kv-remove" onclick="this.parentElement.remove()">✕</button>';$(id).appendChild(r);}
function collectKv(id){const res=[];$(id).querySelectorAll('.kv-row').forEach(r=>{const k=r.querySelector('.kv-key').value.trim(),v=r.querySelector('.kv-val').value.trim();if(k&&v)res.push({key:resolve(k),value:resolve(v)});});return res;}

// === PRE-REQUEST SCRIPT ===
function runPreReq(){const s=$('prereq-script').value.trim();if(!s)return;const lb={setVariable:(k,v)=>{envVars[curEnv][k]=v;},getVariable:k=>envVars[curEnv]?.[k]||'',env:curEnv};try{new Function('lb',s)(lb);}catch(e){console.warn('Pre-req error:',e);}}

// === TEST RUNNER (lb.* API) ===
function runTests(respObj){const s=$('test-script').value.trim();const term=$('test-output');if(!s){term.innerHTML='<span class="text-muted">// No tests</span>';return;}
let pass=0,fail=0,log='';const lb={response:{status:respObj.status||200,json:()=>respObj.data||respObj,headers:respObj.headers||{}},test:(name,fn)=>{try{fn();pass++;log+=`✅ ${name}\n`;}catch(e){fail++;log+=`❌ ${name}: ${e.message}\n`;}},expect:v=>({toBe:x=>{if(v!==x)throw new Error(`${v} ≠ ${x}`);},toContain:x=>{if(typeof v==='string'&&!v.includes(x))throw new Error(`missing "${x}"`);if(Array.isArray(v)&&!v.includes(x))throw new Error(`missing ${x}`);},toBeTruthy:()=>{if(!v)throw new Error(`${v} is falsy`);},toBeGreaterThan:x=>{if(!(v>x))throw new Error(`${v} <= ${x}`);}}),setVariable:(k,val)=>{envVars[curEnv][k]=val;},getVariable:k=>envVars[curEnv]?.[k]||''};
try{new Function('lb',s)(lb);term.textContent=`🏁 ${pass} passed, ${fail} failed\n${log}`;}catch(e){term.textContent=`💥 ${e.message}`;}}

// === REST REQUEST ===
async function sendRequest(){const viewer=$('resp-viewer'),badge=$('resp-badge'),lat=$('resp-lat'),sz=$('resp-size'),hex=$('hex-viewer');
try{runPreReq();const raw=resolve($('api-url').value.trim());
if(raw.startsWith('mock://')){let p='/'+raw.replace(/^mock:\/\/[^\/]+\/?/i,'');const m=mocks[p];badge.innerText=m?'200 MOCK':'404';viewer.textContent=m?JSON.stringify(JSON.parse(m),null,4):'Not found';return;}
let c=raw.replace(/^https?:\/\//i,'');const si=c.indexOf('/');const host=si!==-1?c.substring(0,si):c;const path=si!==-1?c.substring(si):'/';
viewer.textContent='// Connecting…';badge.innerText='…';
const hdrs=collectKv('hdr-matrix');const ck=Object.keys(cookies);if(ck.length>0)hdrs.push({key:'Cookie',value:ck.map(k=>`${k}=${cookies[k]}`).join('; ')});
const body=resolve($('req-body').value.trim());const form=collectKv('form-matrix');
const res=await fetch(`${API}/request`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:'api_studio_run',method,hostname:host,path,headers:hdrs,body_payload:body,form_data_payload:form,test_script_payload:$('test-script').value})});
const data=await res.json();
badge.innerText=data.engine_status||'200';badge.style.color=data.engine_status>=400?'var(--accent-red)':'var(--accent-green)';
const r=data.network_data?JSON.stringify(data.network_data,null,4):JSON.stringify(data,null,4);respCache=r;viewer.textContent=r;hex.textContent=hexDump(r);
const bs=new Blob([r]).size;sz.innerText=bs>=1024?`${(bs/1024).toFixed(1)}KB`:`${bs}B`;lat.innerText=data.telemetry?`${data.telemetry.c_core_duration_us.toFixed(1)}µs`:'0µs';
$('resp-proto').innerText='REST';
if(data.native_test_logs&&data.native_test_logs!=='// Skipped')$('test-output').textContent=data.native_test_logs;
runTests({status:data.engine_status||200,data:data.network_data||data});
}catch(e){viewer.textContent=`Error: ${e.message}`;badge.innerText='ERR';}}

// === GRAPHQL ===
async function sendGraphQL(){const viewer=$('resp-viewer'),badge=$('resp-badge');
try{const host=$('gql-host').value.trim();const path=$('gql-path').value.trim()||'/graphql';const query=$('gql-query').value.trim();let vars={};try{vars=JSON.parse($('gql-vars').value||'{}');}catch{}
viewer.textContent='// Querying…';
const res=await fetch(`${API}/graphql`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({hostname:host,path,query,variables:vars})});
const data=await res.json();const r=JSON.stringify(data.data||data,null,4);respCache=r;viewer.textContent=r;badge.innerText=data.engine_status||200;$('resp-proto').innerText='GraphQL';
}catch(e){viewer.textContent=`Error: ${e.message}`;}}

// === WEBSOCKET ===
async function wsConnect(){const url=$('ws-url').value.trim();const log=$('ws-log');log.textContent=`Connecting to ${url}…\n`;
try{const res=await fetch(`${API}/ws_connect`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url,message:''})});
const data=await res.json();log.textContent+=`Connected! Server: ${data.response||'OK'}\n`;$('resp-proto').innerText='WebSocket';
}catch(e){log.textContent+=`Error: ${e.message}\n`;}}
async function wsSend(){const url=$('ws-url').value.trim();const msg=$('ws-msg').value.trim();const log=$('ws-log');if(!msg)return;
log.textContent+=`→ ${msg}\n`;
try{const res=await fetch(`${API}/ws_connect`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url,message:msg})});
const data=await res.json();log.textContent+=`← ${data.response}\n`;log.scrollTop=log.scrollHeight;
}catch(e){log.textContent+=`Error: ${e.message}\n`;}}

// === gRPC ===
async function grpcInvoke(){const out=$('grpc-output');out.textContent='Invoking…';
try{let payload={};try{payload=JSON.parse($('grpc-payload').value||'{}');}catch{}
const res=await fetch(`${API}/grpc_invoke`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:$('grpc-target').value.trim(),service:$('grpc-service').value.trim(),method:$('grpc-method').value.trim(),payload})});
const data=await res.json();out.textContent=JSON.stringify(data,null,4);$('resp-proto').innerText='gRPC';
}catch(e){out.textContent=`Error: ${e.message}`;}}

// === MQTT ===
async function mqttPublish(){const log=$('mqtt-log');
try{const res=await fetch(`${API}/mqtt_connect`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({broker:$('mqtt-broker').value.trim(),port:parseInt($('mqtt-port').value),topic:$('mqtt-topic').value.trim(),payload:$('mqtt-payload').value.trim(),action:'publish'})});
const data=await res.json();log.textContent+=`📤 Published to ${data.topic}\n`;$('resp-proto').innerText='MQTT';
}catch(e){log.textContent+=`Error: ${e.message}\n`;}}
async function mqttSubscribe(){const log=$('mqtt-log');log.textContent+='Subscribing…\n';
try{const res=await fetch(`${API}/mqtt_connect`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({broker:$('mqtt-broker').value.trim(),port:parseInt($('mqtt-port').value),topic:$('mqtt-topic').value.trim(),action:'subscribe',listen_sec:3})});
const data=await res.json();(data.messages||[]).forEach(m=>log.textContent+=`📥 [${m.topic}] ${m.payload}\n`);if(!data.messages?.length)log.textContent+='No messages received\n';
}catch(e){log.textContent+=`Error: ${e.message}\n`;}}

// === RESPONSE FILTER ===
function filterResp(){const f=$('resp-filter').value.trim();const v=$('resp-viewer');if(!respCache)return;if(!f){v.textContent=respCache;return;}
const lines=respCache.split('\n').filter(l=>l.toLowerCase().includes(f.toLowerCase()));v.textContent=lines.length?lines.join('\n'):'// No matches';}

// === VISUALIZER ===
function runVisualizer(){const code=$('viz-code').value;const frame=$('viz-frame');let data={};try{data=JSON.parse(respCache);}catch{}
const html=`<html><body><script>const data=${JSON.stringify(data)};${code}<\/script></body></html>`;frame.srcdoc=html;}
