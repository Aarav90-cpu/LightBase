// LightBase Studio — Features Module (loaded after app.js)

// === SQL ===
async function runSQL(){const q=$('sql-input').value.trim();const out=$('sql-output');if(!q)return;out.textContent='Executing…';
try{const res=await fetch(`${API}/query`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:'execute_query',db_path:getDb(),query:q})});
const data=await res.json();out.textContent=JSON.stringify(data.db_data||data,null,4);$('sql-lat').innerText=data.telemetry?`${data.telemetry.c_core_duration_us.toFixed(1)}µs`:'0µs';refreshSchemaTree();
}catch(e){out.textContent=`Error: ${e.message}`;}}

// === SCHEMA ===
async function refreshSchemaTree(){const c=$('schema-tree');
try{const res=await fetch(`${API}/query`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({target:'get_schema_tree',db_path:getDb()})});
const data=await res.json();c.innerHTML='';if(!data.tables?.length){c.innerHTML='<span class="text-muted">No tables</span>';return;}
data.tables.forEach(t=>{const el=document.createElement('div');el.innerHTML=`<div class="tree-node" onclick="this.nextElementSibling.style.display=this.nextElementSibling.style.display==='none'?'block':'none'">🗂 ${t.name}</div><div class="tree-children">${t.columns.map(c=>`<div class="tree-leaf">• ${c}</div>`).join('')}</div>`;c.appendChild(el);});
}catch(e){c.innerHTML=`<span class="text-red">${e.message}</span>`;}}

// === COLLECTIONS ===
async function saveCollection(){const name=$('col-name').value.trim();const url=$('api-url').value.trim();if(!name)return;
try{await fetch(`${API}/fs/save`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({category:'collections',name,data:{name,method,url,headers:collectKv('hdr-matrix'),body:$('req-body').value,test_script:$('test-script').value,prereq_script:$('prereq-script').value}})});
$('col-name').value='';loadCollections();
}catch(e){console.error(e);}}
async function loadCollections(){const c=$('col-list');
try{const res=await fetch(`${API}/fs/list`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({category:'collections'})});
const data=await res.json();c.innerHTML='';if(!data.files?.length){c.innerHTML='<span class="text-muted">None</span>';return;}
data.files.forEach(f=>{const el=document.createElement('div');el.className='collection-item';el.innerHTML=`<span class="collection-method">📁</span><span>${f}</span>`;
el.onclick=async()=>{const r=await fetch(`${API}/fs/load`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({category:'collections',name:f})});
const d=await r.json();if(d.url)$('api-url').value=d.url;if(d.method){method=d.method;document.querySelectorAll('.method-btn').forEach(b=>b.classList.toggle('active',b.innerText.trim()===method));}
if(d.body)$('req-body').value=d.body;if(d.test_script)$('test-script').value=d.test_script;if(d.prereq_script)$('prereq-script').value=d.prereq_script;};
c.appendChild(el);});
// Also populate monitor dropdown
const sel=$('mon-col');if(sel){sel.innerHTML='';data.files.forEach(f=>{const o=document.createElement('option');o.value=f;o.textContent=f;sel.appendChild(o);});}
}catch(e){c.innerHTML=`<span class="text-muted">${e.message}</span>`;}}

// === TELEMETRY ===
async function pollMetrics(){try{const res=await fetch(`${API}/sys_metrics`,{method:'POST'});const data=await res.json();const m=data.system_telemetry;
$('cpu-val').innerText=`${m.cpu_usage.toFixed(1)}%`;cpuHist.push(m.cpu_usage);if(cpuHist.length>MAX_H)cpuHist.shift();drawCpu();
const tot=Math.round(m.mem_total_kb/1024),avl=Math.round(m.mem_avail_kb/1024),used=tot-avl;
$('mem-val').innerText=`${used} / ${tot} MB`;$('mem-bar').style.width=`${tot>0?(used/tot*100):0}%`;$('log-slot').innerText=m.last_logged_slot??'—';
}catch{}}
function drawCpu(){const cv=$('cpu-canvas');if(!cv)return;const ctx=cv.getContext('2d'),w=cv.width,h=cv.height;ctx.clearRect(0,0,w,h);
ctx.strokeStyle='rgba(30,45,69,0.6)';ctx.lineWidth=1;for(let y=0;y<h;y+=30){ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke();}
if(cpuHist.length<2)return;const g=ctx.createLinearGradient(0,0,0,h);g.addColorStop(0,'rgba(59,130,246,0.3)');g.addColorStop(1,'rgba(59,130,246,0)');
ctx.beginPath();for(let i=0;i<cpuHist.length;i++){const x=(i/(MAX_H-1))*w,y=h-(cpuHist[i]/100)*h;i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);}
ctx.lineTo(((cpuHist.length-1)/(MAX_H-1))*w,h);ctx.lineTo(0,h);ctx.fillStyle=g;ctx.fill();
ctx.beginPath();ctx.strokeStyle='#3b82f6';ctx.lineWidth=2;for(let i=0;i<cpuHist.length;i++){const x=(i/(MAX_H-1))*w,y=h-(cpuHist[i]/100)*h;i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);}ctx.stroke();}
async function loadTelemetry(){const p=$('telem-pane');p.textContent='Loading…';
try{const res=await fetch(`${API}/telemetry_history`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({max_records:50})});
const data=await res.json();p.textContent=data.telemetry_history?.length?JSON.stringify(data.telemetry_history,null,2):'// No records';
}catch(e){p.textContent=`Error: ${e.message}`;}}

// === COOKIES ===
function clearCookies(){for(const k in cookies)delete cookies[k];paintCookies();}
function paintCookies(){const el=$('cookie-list');const ks=Object.keys(cookies);if(!ks.length){el.innerHTML='<span class="text-muted">Empty</span>';return;}
el.innerHTML=ks.map(k=>`<div style="display:flex;justify-content:space-between;padding:2px 0;border-bottom:1px dashed var(--border)"><span class="text-muted">${k}:</span><span class="text-cyan" style="font-weight:600">${cookies[k]}</span></div>`).join('');}

// === GIT REACTIVE STATE ENGINE ===
let gitSSE = null;
let gitAIContext = null; // Branch-aware AI context

async function pollGit(){const el=$('git-output');el.textContent='Syncing…';
try{const res=await fetch(`${API}/git_sync`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({repo_path:'/home/aarav/LightBase'})});
const data=await res.json();el.textContent=data.git_status||'No data';
}catch(e){el.textContent=`Error: ${e.message}`;}}

function initGitSSE() {
    if (gitSSE) { gitSSE.close(); gitSSE = null; }

    const dot = $('git-sse-dot');
    const banner = $('git-reactive-banner');

    gitSSE = new EventSource(`${API}/events`);

    gitSSE.addEventListener('connected', () => {
        if (dot) { dot.style.background = '#00e676'; dot.title = 'SSE connected'; }
        console.log('[Git SSE] Connected to reactive event stream');
        // Fetch initial state
        fetch(`${API}/git_watch_state`, {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({})})
            .then(r => r.json())
            .then(data => {
                const s = data.git_reactive;
                if (s && s.branch) {
                    const bb = $('git-branch-badge');
                    const sha = $('git-sha');
                    if (bb) bb.textContent = '⎇ ' + s.branch;
                    if (sha) sha.textContent = s.head_sha || '';
                    const el = $('git-output');
                    if (el) el.textContent = `Branch: ${s.branch}\nSHA: ${s.head_sha}\nStatus: watching`;
                }
            }).catch(() => {});
    });

    gitSSE.addEventListener('git_state', (e) => {
        try {
            const data = JSON.parse(e.data);
            handleGitStateEvent(data);
        } catch (err) {
            console.error('[Git SSE] Parse error:', err);
        }
    });

    gitSSE.onerror = () => {
        if (dot) { dot.style.background = '#ff5252'; dot.title = 'SSE disconnected'; }
        console.warn('[Git SSE] Connection lost, reconnecting in 5s...');
    };
}

function handleGitStateEvent(data) {
    const bb = $('git-branch-badge');
    const sha = $('git-sha');
    const banner = $('git-reactive-banner');
    const el = $('git-output');

    // Update branch badge and SHA
    if (bb) bb.textContent = '⎇ ' + (data.branch || '—');
    if (sha) sha.textContent = data.head_sha || '';

    // Build status text
    const lines = [`Event: ${data.event_type}`, `Branch: ${data.branch}`];
    if (data.prev_branch && data.event_type === 'branch_switch') lines.push(`From: ${data.prev_branch}`);
    lines.push(`SHA: ${data.head_sha}`);
    lines.push(`Changed: ${data.changed_file_count} files`);
    if (data.schema_changed) lines.push('⚡ Schema modified');
    if (data.config_changed) lines.push('📂 Configs updated');
    if (data.plugin_changed) lines.push('🐍 Plugins changed');
    if (el) el.textContent = lines.join('\n');

    // Show reactive banner for important events
    if (banner && data.event_type !== 'init') {
        let bannerText = '';
        let bannerColor = '';

        if (data.event_type === 'branch_switch') {
            bannerText = `⚡ Branch switched: ${data.prev_branch} → ${data.branch}`;
            bannerColor = 'linear-gradient(135deg, #1a237e, #0d47a1)';
        } else if (data.event_type === 'head_update') {
            bannerText = `🔄 HEAD updated on ${data.branch} (${data.head_sha})`;
            bannerColor = 'linear-gradient(135deg, #1b5e20, #2e7d32)';
        }

        if (bannerText) {
            banner.textContent = bannerText;
            banner.style.background = bannerColor;
            banner.style.color = '#fff';
            banner.style.display = 'block';
            // Auto-hide after 8 seconds
            setTimeout(() => { banner.style.display = 'none'; }, 8000);
        }
    }

    // === HOT-RELOAD ACTIONS ===
    const actions = data.reload_actions || [];

    if (actions.includes('schema_reload')) {
        console.log('[Git Reactor] 🗄️ Hot-reloading schema tree...');
        if (typeof refreshSchemaTree === 'function') refreshSchemaTree();
    }

    if (actions.includes('config_reload')) {
        console.log('[Git Reactor] 📂 Hot-reloading collections & environments...');
        if (typeof loadCollections === 'function') loadCollections();
        if (typeof loadFlows === 'function') loadFlows();
    }

    if (actions.includes('plugin_reload')) {
        console.log('[Git Reactor] 🐍 Hot-reloading plugin list...');
        if (typeof loadPlugins === 'function') loadPlugins();
    }

    if (actions.includes('ai_context_update') && data.ai_context) {
        console.log('[Git Reactor] 🤖 AI context updated for branch:', data.ai_context.active_branch);
        gitAIContext = data.ai_context;
    }

    // Log all changed files in console for debugging
    if (data.changed_files && data.changed_files.length > 0) {
        console.log(`[Git Reactor] Changed files (${data.changed_file_count}):`, data.changed_files);
    }
}


// === VAULT ===
async function vaultEncrypt(){const key=$('vault-key').value.trim();const lbl=$('vault-status');if(!key)return;lbl.innerHTML='Encrypting…';
try{const res=await fetch(`${API}/secure_vault_key`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({provider:$('vault-provider').value,plain_key:key})});
const data=await res.json();lbl.innerHTML=data.engine_status===200?'<span class="text-green">✅ Secured</span>':'<span class="text-red">❌ Failed</span>';$('vault-key').value='';
}catch(e){lbl.innerHTML=`<span class="text-red">${e.message}</span>`;}}

// === DOCS ===
async function genDocs(){const p=$('docs-pane');p.value='Compiling…';
try{const res=await fetch(`${API}/autogen_docs`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({db_path:getDb()})});
const data=await res.json();p.value=data.compiled_markdown||'# Error';
}catch(e){p.value=`Error: ${e.message}`;}}
async function exportOpenAPI(){const p=$('docs-pane');p.value='Generating OpenAPI…';
try{const res=await fetch(`${API}/export_openapi`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({})});
const data=await res.json();p.value=JSON.stringify(data.spec||data,null,2);
}catch(e){p.value=`Error: ${e.message}`;}}

// === AI ===
async function aiInfer(){const inp=$('ai-input'),vp=$('ai-output');const prompt=inp.value.trim();if(!prompt)return;vp.textContent='🤖 Processing…';inp.value='';
try{const sd=await fetch(`${API}/autogen_docs`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({db_path:getDb()})});
const schema=(await sd.json()).compiled_markdown||'';
const full=`System: LightBase AI Copilot. DB: ${getDb()}\nSchema:\n${schema}\n\nUser: ${prompt}\n\nAssistant:`;
const res=await fetch(`${API}/ai_inference`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({context_payload:full})});
const data=await res.json();vp.textContent=data.ai_generation||'No response';
}catch(e){vp.textContent=`Error: ${e.message}`;}}

// === FLOWS ===
function addFlowNode(type){flowIdCounter++;
const node={id:`node_${flowIdCounter}`,type,method:'GET',url:'',condition:'',transform:'',prompt:''};flowNodes.push(node);renderFlow();}
function renderFlow(){const canvas=$('flow-canvas');canvas.innerHTML='';
flowNodes.forEach((n,i)=>{const el=document.createElement('div');el.className=`flow-node ${n.type}`;el.draggable=true;
let inner=`<div class="flow-node-header"><span>${n.type.toUpperCase()} #${i+1}</span><button class="kv-remove" onclick="flowNodes.splice(${i},1);renderFlow()">✕</button></div>`;
if(n.type==='request')inner+=`<input placeholder="GET" value="${n.method}" onchange="flowNodes[${i}].method=this.value"><input placeholder="https://..." value="${n.url}" onchange="flowNodes[${i}].url=this.value">`;
else if(n.type==='condition')inner+=`<input placeholder="response.status===200" value="${n.condition}" onchange="flowNodes[${i}].condition=this.value">`;
else if(n.type==='transform')inner+=`<textarea placeholder="return data.map(…)" onchange="flowNodes[${i}].transform=this.value">${n.transform}</textarea>`;
else if(n.type==='ai')inner+=`<textarea placeholder="Summarize this data…" onchange="flowNodes[${i}].prompt=this.value">${n.prompt}</textarea>`;
el.innerHTML=inner;canvas.appendChild(el);});}
async function saveFlow(){const name=$('flow-name').value.trim()||'untitled';
await fetch(`${API}/flow/save`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,data:{nodes:flowNodes}})});loadFlows();}
async function loadFlows(){const c=$('flow-list');
try{const res=await fetch(`${API}/fs/list`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({category:'flows'})});
const data=await res.json();c.innerHTML='';(data.files||[]).forEach(f=>{const el=document.createElement('div');el.className='collection-item';el.innerHTML=`<span class="collection-method">🔀</span><span>${f}</span>`;
el.onclick=async()=>{const r=await fetch(`${API}/flow/load`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:f})});
const d=await r.json();if(d.nodes){flowNodes=d.nodes;renderFlow();}};c.appendChild(el);});
}catch{}}
async function runFlow(){const out=$('flow-output');out.textContent='Running flow…';
const name=$('flow-name').value.trim()||'untitled';
try{await saveFlow();const res=await fetch(`${API}/flow/run`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name})});
const data=await res.json();out.textContent=JSON.stringify(data.flow_results||data,null,2);
}catch(e){out.textContent=`Error: ${e.message}`;}}

// === MONITORS ===
async function runMonitorOnce(){const out=$('mon-output');const col=$('mon-col').value;out.textContent='Running…';
try{const res=await fetch(`${API}/run_monitor`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({collection:col})});
const data=await res.json();out.textContent=JSON.stringify(data,null,2);
}catch(e){out.textContent=`Error: ${e.message}`;}}
async function scheduleMonitor(){const col=$('mon-col').value;const interval=parseInt($('mon-interval').value)||60;
try{const res=await fetch(`${API}/monitor/schedule`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({collection:col,interval_sec:interval})});
const data=await res.json();const el=document.createElement('div');el.className='hist-item';el.innerHTML=`<span class="hist-method text-green">⏱</span><span class="hist-url">${col} every ${interval}s</span><span class="hist-status text-cyan">${data.monitor_id}</span><button class="btn btn-xs btn-danger" onclick="stopMonitor('${data.monitor_id}');this.parentElement.remove()">Stop</button>`;
$('mon-list').appendChild(el);
}catch(e){$('mon-output').textContent=`Error: ${e.message}`;}}
async function stopMonitor(id){await fetch(`${API}/monitor/stop`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({monitor_id:id})});}

// === HISTORY ===
async function loadHistory(){const c=$('hist-list');const q=($('hist-search')?.value||'').toLowerCase();
try{const res=await fetch(`${API}/history/list`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({})});
const data=await res.json();c.innerHTML='';(data.history||[]).forEach(h=>{if(!h)return;const url=h.url||'';const proto=(h.protocol||'REST').toLowerCase();
if(q&&!url.toLowerCase().includes(q)&&!proto.includes(q))return;
const el=document.createElement('div');el.className='hist-item';
el.innerHTML=`<span class="hist-proto ${proto}">${h.protocol||'REST'}</span><span class="hist-method" style="color:var(--accent-green)">${h.method||'—'}</span><span class="hist-url">${url}</span><span class="hist-status" style="color:${(h.status||200)<400?'var(--accent-green)':'var(--accent-red)'}">${h.status||'—'}</span><span class="hist-time">${h.timestamp?new Date(h.timestamp).toLocaleTimeString():''}</span>`;
el.onclick=()=>{if(h.url)$('api-url').value=h.url;switchTab('api',document.querySelectorAll('.tab')[1]);};
c.appendChild(el);});
}catch{}}
async function clearHistory(){await fetch(`${API}/history/clear`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({})});loadHistory();}

// === STRESS TEST ===
async function runStress(){const n=parseInt($('stress-n').value)||10;const d=parseInt($('stress-d').value)||100;const log=$('stress-log');
const raw=resolve($('api-url').value.trim());let c=raw.replace(/^https?:\/\//i,'');const si=c.indexOf('/');const host=si!==-1?c.substring(0,si):c;const path=si!==-1?c.substring(si):'/get';
log.textContent=`Dispatching ${n} requests…\n`;let done=0,tot=0;
for(let i=1;i<=n;i++){try{const res=await fetch(`${API}/request`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({method,hostname:host,path,headers:[]})});
const data=await res.json();const lat=data.telemetry?.c_core_duration_us||0;tot+=lat;done++;log.textContent+=`⚡ [${i}/${n}] ${lat.toFixed(1)}µs\n`;log.scrollTop=log.scrollHeight;
}catch{log.textContent+=`❌ [${i}] Timeout\n`;}if(i<n)await new Promise(r=>setTimeout(r,d));}
log.textContent+=`\n🏁 Avg: ${(tot/done).toFixed(1)}µs\n`;}

// ============================================================================
// PYTHON PLUGIN SYSTEM
// ============================================================================
async function savePlugin(){const name=$('plugin-name').value.trim()||'untitled';const code=$('plugin-code').value;
try{await fetch(`${API}/plugin/save`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,code})});
$('plugin-output').textContent=`✅ Saved: ${name}.py`;loadPlugins();
}catch(e){$('plugin-output').textContent=`Error: ${e.message}`;}}

async function runPlugin(){const name=$('plugin-name').value.trim();const out=$('plugin-output');if(!name){out.textContent='Enter plugin name first';return;}
out.textContent='Running…';
const context={url:$('api-url')?.value||'',method,body:$('req-body')?.value||'',response:respCache?JSON.parse(respCache):{}};
try{const res=await fetch(`${API}/plugin/run`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name,context})});
const data=await res.json();out.textContent=data.plugin_output?JSON.stringify(data.plugin_output,null,2):(data.error||'No output');
}catch(e){out.textContent=`Error: ${e.message}`;}}

async function loadPlugins(){const c=$('plugin-list');
try{const res=await fetch(`${API}/plugin/list`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({})});
const data=await res.json();c.innerHTML='';
(data.plugins||[]).forEach(p=>{const el=document.createElement('div');el.className='collection-item';
el.innerHTML=`<span class="collection-method">🐍</span><span>${p}</span>`;
el.onclick=()=>{$('plugin-name').value=p;
fetch(`${API}/plugin/load`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:p})}).then(r=>r.json()).then(d=>{
$('plugin-code').value=d.code||'// Could not load plugin';}).catch(()=>{$('plugin-code').value='// Error loading plugin';});};
c.appendChild(el);});
if(!data.plugins?.length)c.innerHTML='<span class="text-muted">None</span>';
}catch{}}

async function pipInstall(){const pkg=$('pip-pkg').value.trim();const out=$('pip-output');if(!pkg)return;
out.textContent=`Installing ${pkg}…`;
try{const res=await fetch(`${API}/plugin/install`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({package:pkg})});
const data=await res.json();out.textContent=data.returncode===0?`✅ Installed ${pkg}\n${data.stdout}`:`❌ Failed\n${data.stderr}`;
}catch(e){out.textContent=`Error: ${e.message}`;}}

// ============================================================================
// AI AGENTIC WORKFLOWS
// ============================================================================
async function aiGenTests(){const out=$('test-output');const viewer=$('resp-viewer');
out.textContent='🤖 Generating tests…';
try{const res=await fetch(`${API}/ai/generate_tests`,{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({response_data:respCache||'',endpoint:$('api-url')?.value||''})});
const data=await res.json();$('test-script').value=data.generated_tests||'// AI could not generate tests';
out.textContent='✅ Tests generated! Click Send to run them.';
}catch(e){out.textContent=`Error: ${e.message}`;}}

async function aiChainNext(){const out=$('test-output');
out.textContent='🔗 AI analyzing response for next request…';
try{const res=await fetch(`${API}/ai/chain_next`,{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({response_data:respCache||'',url:$('api-url')?.value||'',method})});
const data=await res.json();const s=data.suggestion||{};
if(s.url)$('api-url').value=s.url;
if(s.method){method=s.method;document.querySelectorAll('.method-btn').forEach(b=>b.classList.toggle('active',b.innerText.trim()===method));}
if(s.body)$('req-body').value=typeof s.body==='string'?s.body:JSON.stringify(s.body,null,2);
out.textContent=`🔗 Next: ${s.method||'?'} ${s.url||'?'}\n💡 ${s.explanation||s.raw||'No explanation'}`;
}catch(e){out.textContent=`Error: ${e.message}`;}}

async function aiExplain(){const out=$('test-output');
out.textContent='💡 AI explaining response…';
try{const res=await fetch(`${API}/ai/explain`,{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({response_data:respCache||''})});
const data=await res.json();out.textContent=`💡 ${data.explanation||'No explanation available'}`;
}catch(e){out.textContent=`Error: ${e.message}`;}}

// ============================================================================
// LIVE DATA STREAMER
// ============================================================================
async function startStream(){const url=$('stream-url').value.trim();const type=$('stream-type').value;
const dur=parseInt($('stream-dur').value)||10;const log=$('stream-log');
log.textContent=`Connecting to ${url} (${type.toUpperCase()}, ${dur}s)…\n`;$('stream-count').innerText='…';$('stream-rate').innerText='…';
const route=type==='ws'?'/stream/ws_live':'/stream/sse_connect';
try{const res=await fetch(`${API}${route}`,{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({url,duration_sec:dur})});
const data=await res.json();
const msgs=data.messages||data.events||[];
$('stream-count').innerText=msgs.length;$('stream-rate').innerText=data.rate_per_sec||0;
log.textContent='';
msgs.forEach((m,i)=>{const line=`[${i+1}] ${m.data||m.error||JSON.stringify(m)}\n`;log.textContent+=line;});
if(!msgs.length)log.textContent='// No messages received';
log.scrollTop=log.scrollHeight;$('resp-proto').innerText=type.toUpperCase()+' Stream';
}catch(e){log.textContent=`Error: ${e.message}`;}}

// ============================================================================
// JUPYTER NOTEBOOK & PYTHON EXPORT
// ============================================================================
async function exportNotebook(source){const col=source==='history'?'':($('col-name')?.value||'');
const out=source==='history'?$('hist-list'):$('plugin-output');
try{const res=await fetch(`${API}/export/notebook`,{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({collection:col,source:source==='history'?'history':'collection'})});
const data=await res.json();
alert(`📓 Notebook exported!\n\nFile: ${data.filename}\nPath: ${data.path}\nCells: ${data.cell_count}`);
}catch(e){alert(`Export error: ${e.message}`);}}

async function exportPython(source){const col=source==='history'?'':($('col-name')?.value||'');
try{const res=await fetch(`${API}/export/python`,{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({collection:col})});
const data=await res.json();
alert(`🐍 Python script exported!\n\nFile: ${data.filename}\nPath: ${data.path}`);
}catch(e){alert(`Export error: ${e.message}`);}}

// ============================================================================
// BOOT SEQUENCE
// ============================================================================
setInterval(pollMetrics,2000);pollMetrics();
setTimeout(()=>{refreshSchemaTree();loadCollections();pollGit();loadFlows();loadHistory();loadPlugins();initGitSSE();},1000);

