#pragma once
// The sapiens-web dashboard, served at GET /. Self-contained e-ink-styled SPA
// (black & white, high contrast) — lists recordings with per-audio controls:
// play in browser, download .wav, transcribe / re-transcribe, view/download .txt.

namespace sapiens::web {

inline const char* kIndexHtml = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Sapiens Note — Recordings</title>
<style>
  :root { --ink:#000; --paper:#fff; --shadow:#c9c9c9; --muted:#555; }
  * { box-sizing: border-box; }
  body { margin:0; background:var(--paper); color:var(--ink);
         font-family: "Helvetica Neue", Arial, sans-serif; line-height:1.4; }
  header { display:flex; align-items:center; justify-content:space-between;
           gap:14px; padding:16px 20px; border-bottom:3px solid var(--ink); }
  .brand { display:flex; align-items:center; gap:14px; min-width:0; }
  .brand .logo { width:48px; height:48px; display:block; flex:0 0 auto;
                 image-rendering:auto; }
  header h1 { margin:0; font-size:26px; letter-spacing:.5px; line-height:1.05; }
  header .sub { color:var(--muted); font-size:13px; }
  #ver { font-variant-numeric:tabular-nums; }
  main { max-width:980px; margin:0 auto; padding:16px 16px 60px; }
  .empty { text-align:center; color:var(--muted); padding:60px 0; }
  .row { border:2px solid var(--ink); border-radius:12px; padding:16px 18px;
         margin:16px 0; box-shadow:3px 4px 0 var(--shadow); background:var(--paper); }
  .row .top { display:flex; align-items:center; gap:10px; flex-wrap:wrap; }
  .num { font-variant-numeric:tabular-nums; font-weight:700; }
  .title { font-weight:700; flex:1 1 auto; min-width:120px;
           overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
  .badge { font-size:11px; text-transform:uppercase; letter-spacing:.6px;
           border:2px solid var(--ink); border-radius:999px; padding:2px 9px; }
  .badge.done { background:var(--ink); color:var(--paper); }
  .controls { display:flex; align-items:center; gap:10px; flex-wrap:wrap; margin-top:10px; }
  audio { height:34px; filter:grayscale(1) contrast(1.1); }
  a.btn, button.btn { font:inherit; font-size:14px; cursor:pointer;
         border:2px solid var(--ink); border-radius:10px; background:var(--paper);
         color:var(--ink); padding:6px 12px; text-decoration:none;
         box-shadow:2px 3px 0 var(--shadow); }
  a.btn:hover, button.btn:hover { background:var(--ink); color:var(--paper); }
  .btn svg { vertical-align:middle; display:inline-block; }   /* icons follow color */
  .btn .sz { vertical-align:middle; margin-left:6px; font-variant-numeric:tabular-nums; }
  .act svg + svg { margin-left:5px; }                          /* speak + text icons */
  a.btn:active, button.btn:active { box-shadow:none; transform:translate(2px,3px); }
  button.btn[disabled] { opacity:.5; cursor:default; box-shadow:none; }
  .tx { margin-top:10px; border-top:1px dashed var(--ink); padding-top:10px; display:none; }
  .tx.open { display:block; }
  .tx pre { white-space:pre-wrap; word-wrap:break-word; margin:0 0 8px;
            font-family: ui-monospace, Menlo, Consolas, monospace; font-size:13px; }
  .tx a.btn { display:block; width:max-content; margin-left:auto; } /* Download .txt → right-aligned */
  /* now-playing tile: invert the title strip (e-ink flash) + deeper shadow */
  .row.playing { box-shadow:5px 6px 0 var(--ink); }
  .row.playing .top { background:var(--ink); color:var(--paper);
                      border-radius:8px; padding:4px 8px; margin:-4px -8px 8px; }
  .row.playing .badge { border-color:var(--paper); }
  .row.playing .badge.done { background:var(--paper); color:var(--ink); }
  /* confirm modal (matches the simulator's delete prompt) */
  .modal-bg { position:fixed; inset:0; background:rgba(0,0,0,.35);
              display:none; align-items:center; justify-content:center; }
  .modal-bg.open { display:flex; }
  .modal { background:var(--paper); border:3px solid var(--ink); border-radius:14px;
           box-shadow:5px 6px 0 var(--shadow); padding:18px 20px; width:90%; max-width:360px;
           max-height:88vh; overflow:auto; }
  .modal h2 { margin:0 0 6px; }
  .modal .mtitle { font-weight:700; margin:6px 0; overflow:hidden;
                   text-overflow:ellipsis; white-space:nowrap; }
  .modal .mbtns { display:flex; gap:12px; justify-content:flex-end; margin-top:14px; }
  .modal pre { white-space:pre-wrap; word-break:break-word; margin:8px 0;
               font-family: ui-monospace, Menlo, Consolas, monospace; font-size:13px;
               background:#f2f2f2; border:1px solid var(--ink); border-radius:6px;
               padding:8px; max-height:160px; overflow:auto; }
  .modal .hint { color:var(--muted); font-size:13px; margin:6px 0 0;
                 overflow-wrap:anywhere; word-break:break-word; }
  .modal code { overflow-wrap:anywhere; word-break:break-all; }
  .modal .now { font-size:13px; margin:4px 0 10px; padding:8px 10px; border-radius:8px;
                background:#f2f2f2; border:1px solid var(--line); overflow-wrap:anywhere; }
  .modal label { display:block; font-size:13px; color:var(--muted); margin:10px 0 3px; }
  .modal input[type=text], .modal input[type=password], .modal input[type=number],
  .modal select {
                 width:100%; box-sizing:border-box; padding:11px; border:2px solid var(--ink);
                 border-radius:8px; font-size:15px; background:var(--paper); color:var(--ink); }
  .mbtns .btn svg { vertical-align:-3px; margin-right:4px; }
  .spin { display:inline-block; width:16px; height:16px; vertical-align:-3px;
          border:3px solid var(--line); border-top-color:var(--ink); border-radius:50%;
          animation: spin .8s linear infinite; }
  @keyframes spin { to { transform: rotate(360deg); } }
  #hdrspin { display:none; margin-left:8px; }
  #hdrspin.on { display:inline-block; }
  .title { cursor:pointer; } .title:hover { text-decoration:underline; }
  .modal .sec { margin-top:14px; padding-top:10px; border-top:1px solid var(--line);
                font-weight:700; font-size:14px; }
  .row2 { display:flex; gap:10px; } .row2 > div { flex:1; }
  .seg { display:flex; gap:8px; margin:8px 0 2px; }
  .seg .opt { flex:1; display:flex; align-items:center; gap:8px; cursor:pointer;
              border:2px solid var(--ink); border-radius:8px; padding:10px; font-size:14px;
              color:var(--ink); margin:0; }
  .seg .opt input { width:auto; accent-color:var(--ink); }
  .modal .mbtns { flex-wrap:wrap; }
  .mbtns .grow { margin-right:auto; } /* push a button to the left edge of the row */
  /* sound spectrum shown in the playing tile */
  .spec { vertical-align:middle; border:2px solid var(--ink); border-radius:8px;
          background:var(--paper); display:none; margin-left:auto; } /* flush right */
  .row.playing .spec { display:inline-block; }
  /* transcription progress (indeterminate — the engine reports no %) */
  .prog { display:none; flex-basis:100%; height:10px; margin-top:8px; overflow:hidden;
          border:2px solid var(--ink); border-radius:6px; background:var(--paper); }
  .row.busy .prog { display:block; }
  .prog .bar { width:35%; height:100%; background:var(--ink); will-change:margin-left;
               animation: prog 1.1s ease-in-out infinite; }
  @keyframes prog { 0% { margin-left:-35%; } 100% { margin-left:100%; } }
  .pager { display:none; align-items:center; justify-content:center; gap:10px; margin:20px 0; flex-wrap:wrap; }
  .pager .nums { display:flex; gap:8px; flex-wrap:wrap; }
  .btn.pg { min-width:40px; text-align:center; font-variant-numeric:tabular-nums; }
  .btn.pg.active { background:var(--ink); color:var(--paper); } /* current page */
</style>
</head>
<body>
<header>
  <div class="brand">
    <img class="logo" src="{{LOGO}}" alt="Sapiens Note logo">
    <div>
      <h1>Sapiens Note</h1>
      <div class="sub">local dashboard · <span id="engine">engine: …</span>
        · <span id="ver">…</span></div>
    </div>
  </div>
  <div style="display:flex;align-items:center;gap:12px">
    <span class="sub" id="sub">recordings</span>
    <span id="hdrspin" class="spin" title="Reading device…"></span>
    <button class="btn" id="cfgbtn" title="Settings">Settings</button>
  </div>
</header>
<main>
  <div id="list"><div class="empty">Loading…</div></div>
  <div id="pager" class="pager">
    <button class="btn" id="prev">‹ Prev</button>
    <span class="nums" id="nums"></span>
    <button class="btn" id="next">Next ›</button>
  </div>
</main>
<div id="modal" class="modal-bg">
  <div class="modal">
    <h2>Delete this note?</h2>
    <p class="mtitle"></p>
    <p>This cannot be undone.</p>
    <div class="mbtns">
      <button class="btn mdel">Delete</button>
      <button class="btn mcancel">Cancel</button>
    </div>
  </div>
</div>
<div id="notice" class="modal-bg">
  <div class="modal">
    <h2 class="ntitle"></h2>
    <pre class="nmsg"></pre>
    <p class="hint nhint"></p>
    <div class="mbtns"><button class="btn nok">OK</button></div>
  </div>
</div>
<div id="remodal" class="modal-bg">
  <div class="modal">
    <h2>Already transcribed</h2>
    <p class="rtitle"></p>
    <p>Re-transcribe and replace it? The current transcript will be lost.</p>
    <div class="mbtns">
      <button class="btn rredo">Re-transcribe</button>
      <button class="btn rcancel">Cancel</button>
    </div>
  </div>
</div>
<div id="cfgmodal" class="modal-bg">
  <div class="modal">
    <h2>Settings</h2>
    <div class="sec" style="border-top:0;margin-top:0;padding-top:0">Transcription</div>
    <div class="now cfgnow">Loading…</div>
    <div class="seg">
      <label class="opt"><input type="radio" name="prov" value="openai" id="provoa">OpenAI</label>
      <label class="opt"><input type="radio" name="prov" value="local" id="provlocal">Local server</label>
    </div>
    <div id="urlrow">
      <label for="cfgurl">Local server URL</label>
      <input id="cfgurl" type="text" autocomplete="off"
             placeholder="http://192.168.1.x:PORT/v1/audio/transcriptions">
    </div>
    <label id="keylbl" for="cfgkey">OpenAI API key</label>
    <input id="cfgkey" type="password" autocomplete="off" placeholder="API key">
    <p class="hint cfghint"></p>
    <div class="sec">Device</div>
    <div class="row2">
      <div>
        <label for="cfgsleep">Sleep after (s, 0 = never)</label>
        <input id="cfgsleep" type="number" min="0" max="3600" inputmode="numeric">
      </div>
      <div>
        <label for="cfghold">Hold to record (s)</label>
        <input id="cfghold" type="number" min="1" max="10" inputmode="numeric">
      </div>
    </div>
    <label for="cfgscroll">Text scroll speed</label>
    <input id="cfgscroll" type="number" min="1" inputmode="numeric">
    <label for="cfgname">Name format</label>
    <select id="cfgname"></select>
    <label style="display:flex;align-items:center;gap:8px;cursor:pointer;margin-top:12px">
      <input id="cfgdisk" type="checkbox" style="width:auto"> Disk mode (show as a USB drive)
    </label>
    <p class="hint">Enabling reboots the device into USB-drive mode (recordings + .txt
       on your PC). The serial/web are off while in disk mode — hold the TOP button to exit.</p>
    <div class="mbtns" style="justify-content:space-between">
      <button class="btn csave"><svg width="16" height="16" viewBox="0 0 24 24" fill="none"
        stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6L9 17l-5-5"/></svg>Save</button>
      <button class="btn cclear"><svg width="16" height="16" viewBox="0 0 24 24" fill="none"
        stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h18"/><path d="M8 6V4h8v2"/><path d="M6 6l1 14h10l1-14"/><path d="M10 11v5M14 11v5"/></svg>Remove</button>
      <button class="btn ccancel"><svg width="16" height="16" viewBox="0 0 24 24" fill="none"
        stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6 6l12 12M18 6L6 18"/></svg>Cancel</button>
    </div>
  </div>
</div>
<div id="renamemodal" class="modal-bg">
  <div class="modal">
    <h2>Rename recording</h2>
    <label for="rnname">Name (max 100 chars)</label>
    <input id="rnname" type="text" maxlength="100" autocomplete="off" placeholder="recording name">
    <p class="hint">The .wav file is renamed to
       <code>rec-YYYYMMDD-HHMMSS-name.wav</code> on the SD card.</p>
    <div class="mbtns" style="justify-content:space-between">
      <button class="btn rnsave"><svg width="16" height="16" viewBox="0 0 24 24" fill="none"
        stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6L9 17l-5-5"/></svg>Save</button>
      <button class="btn rncancel"><svg width="16" height="16" viewBox="0 0 24 24" fill="none"
        stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6 6l12 12M18 6L6 18"/></svg>Cancel</button>
    </div>
  </div>
</div>
<script>
const $ = (s, r=document) => r.querySelector(s);
const fmtSize = (b) => { if (!b) return ''; const u=['B','KB','MB','GB']; let i=0,v=b;
  while (v>=1024 && i<u.length-1) { v/=1024; i++; } return (i?v.toFixed(1):v)+' '+u[i]; };
// Inline SVG icons (stroke = currentColor, so they invert with the button).
const SVG = 'viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"';
const ICONS = {
  download: `<svg ${SVG}><path d="M12 3v12"/><path d="M7 10l5 5 5-5"/><path d="M5 21h14"/></svg>`,
  trash:    `<svg ${SVG}><path d="M3 6h18"/><path d="M8 6V4h8v2"/><path d="M6 6l1 14h10l1-14"/><path d="M10 11v5M14 11v5"/></svg>`,
  speak:    `<svg ${SVG}><circle cx="9" cy="8" r="3"/><path d="M3.5 20c0-3 2.5-5 5.5-5s5.5 2 5.5 5"/><path d="M17 8.5c1 1 1 3 0 4"/><path d="M19.5 6.5c2 2 2 6 0 8"/></svg>`,
  text:     `<svg ${SVG}><rect x="5" y="3" width="14" height="18" rx="2"/><path d="M8.5 8h7M8.5 12h7M8.5 16h4"/></svg>`,
};
const rows = new Map();      // id -> row element (built once, reused)
const open = new Set();      // ids whose transcript panel is expanded
const busy = new Set();      // ids currently transcribing
const PAGE_SIZE = 5;         // recordings per page
let page = 0;                // current page (0-based)

// Poll + reconcile: only touch what changed so the <audio> element is never
// recreated (playback keeps going) and open transcript panels stay put.
async function load() {
  const spin = $('#hdrspin'); spin.classList.add('on'); // reading the device
  try {
  let notes = [];
  try { notes = await (await fetch('/api/notes')).json(); }
  catch (e) { return; }
  const all = notes.filter(n => n.audio);
  $('#sub').textContent = all.length + ' recording' + (all.length===1?'':'s');
  const list = $('#list');
  if (!all.length) { list.innerHTML = '<div class="empty">No recordings yet — record one in the simulator.</div>'; rows.clear(); renderPager(1, 0); return; }
  if (list.firstElementChild && list.firstElementChild.classList.contains('empty')) list.innerHTML = '';

  const pages = Math.max(1, Math.ceil(all.length / PAGE_SIZE));
  if (page >= pages) page = pages - 1;
  if (page < 0) page = 0;
  const desired = all.slice(page * PAGE_SIZE, page * PAGE_SIZE + PAGE_SIZE);
  const ids = new Set(desired.map(n => n.id));
  let prev = null;
  for (const n of desired) {
    let row = rows.get(n.id);
    if (!row) { row = createRow(n); rows.set(n.id, row); }
    updateRow(row, n);
    const ref = prev ? prev.nextSibling : list.firstChild; // place in order
    if (ref !== row) list.insertBefore(row, ref);          // moves only new rows
    prev = row;
  }
  for (const [id, row] of [...rows]) if (!ids.has(id)) { row.remove(); rows.delete(id); }
  renderPager(pages, all.length);
  } finally { spin.classList.remove('on'); }
}

function renderPager(pages, total) {
  const p = $('#pager');
  if (pages <= 1) { p.style.display = 'none'; return; }
  p.style.display = 'flex';
  const nums = $('#nums'); nums.innerHTML = '';
  for (let i = 0; i < pages; i++) {                  // numbered page buttons
    const b = document.createElement('button');
    b.className = 'btn pg' + (i === page ? ' active' : '');
    b.textContent = i + 1;
    b.setAttribute('aria-label', 'Page ' + (i + 1));
    b.onclick = () => { if (i !== page) { page = i; load(); window.scrollTo(0, 0); } };
    nums.appendChild(b);
  }
  $('#prev').disabled = page <= 0;
  $('#next').disabled = page >= pages - 1;
}
$('#prev').onclick = () => { if (page > 0) { page--; load(); window.scrollTo(0, 0); } };
$('#next').onclick = () => { page++; load(); window.scrollTo(0, 0); };

function createRow(n) {
  const row = document.createElement('div');
  row.className = 'row';
  const a = encodeURIComponent(n.audio);
  row.innerHTML = `
    <div class="top">
      <span class="num"></span>
      <span class="title"></span>
      <span class="badge"></span>
    </div>
    <div class="controls">
      <audio controls preload="metadata" src="/media/${a}"></audio>
      <a class="btn" href="/media/${a}" download title="Download .wav">${ICONS.download}${n.size ? ` <span class="sz">${fmtSize(n.size)}</span>` : ''}</a>
      <button class="btn act"></button>
      <button class="btn tgl" style="display:none">Transcript</button>
      <button class="btn del" title="Delete">${ICONS.trash}</button>
      <canvas class="spec" width="96" height="28"></canvas>
      <div class="prog"><div class="bar"></div></div>
    </div>
    <div class="tx">
      <pre>loading…</pre>
      <a class="btn" href="/api/transcript/${n.id}?download=1" download>Download .txt</a>
    </div>`;
  $('.act', row).onclick = () => requestTranscribe(n.id, row);
  $('.tgl', row).onclick = () => toggleTx(n.id, row);
  $('.del', row).onclick = () => confirmDelete(n.id, $('.title', row).textContent || n.title);
  $('.title', row).title = 'Click to rename';
  $('.title', row).onclick = () => openRename(n.id, $('.title', row).textContent || '');
  // highlight the tile being played; only one plays at a time
  const au = $('audio', row);
  au.addEventListener('play', () => {
    document.querySelectorAll('.row.playing').forEach(r => {
      if (r !== row) { r.classList.remove('playing'); const o = $('audio', r); if (o && !o.paused) o.pause(); }
    });
    row.classList.add('playing');
    startViz(row, au);
  });
  au.addEventListener('pause', () => { row.classList.remove('playing'); stopViz(row); });
  au.addEventListener('ended', () => { row.classList.remove('playing'); stopViz(row); });
  return row;
}

// Update an existing row in place — never replaces the <audio>.
function updateRow(row, n) {
  const done = n.status === 'transcribed';
  $('.num', row).textContent = String(n.idx).padStart(4,'0');
  const t = $('.title', row); t.textContent = n.title; t.title = n.title;
  const b = $('.badge', row); b.textContent = done ? 'transcribed' : 'pending'; b.classList.toggle('done', done);
  $('.tgl', row).style.display = done ? '' : 'none';
  const act = $('.act', row), isBusy = busy.has(n.id);
  act.disabled = isBusy;
  act.innerHTML = ICONS.speak + ICONS.text;
  act.title = done ? 'Re-transcribe' : 'Transcribe';
  row.classList.toggle('busy', isBusy); // shows the progress bar
  const statusChanged = row.dataset.status !== n.status;
  row.dataset.status = n.status;
  $('.tx', row).classList.toggle('open', open.has(n.id));
  if (open.has(n.id) && (statusChanged || row.dataset.txLoaded !== '1')) fillTx(n.id, row);
}

// Clicking transcribe: confirm first if a transcript already exists.
function requestTranscribe(id, row) {
  if (row && row.dataset.status === 'transcribed')
    openReConfirm(id, $('.title', row).textContent);
  else
    transcribe(id);
}

let cfgResolve = null;
function updateProvUI() {
  const local = $('#provlocal').checked;
  $('#urlrow').style.display = local ? '' : 'none';
  $('#keylbl').textContent = local ? 'API key (optional)' : 'OpenAI API key';
}
function fillConfig(c) {
  const isLocal = !!c.url && !/api\.openai\.com/.test(c.url);
  $('#provoa').checked = !isLocal;
  $('#provlocal').checked = isLocal;
  $('#cfgurl').value = isLocal ? c.url : '';
  $('#cfgkey').value = '';
  $('#cfgsleep').value = (c.sleep_s != null ? c.sleep_s : 15);
  $('#cfghold').value = (c.hold_s != null ? c.hold_s : 3);
  $('#cfgscroll').value = (c.scroll != null ? c.scroll : 1);
  $('#cfgscroll').max = c.scroll_max || 5;
  const sel = $('#cfgname'); sel.innerHTML = '';
  (c.name_examples || []).forEach((s, i) => {
    const o = document.createElement('option'); o.value = i + 1; o.textContent = s; sel.appendChild(o);
  });
  sel.value = (c.name_fmt != null ? c.name_fmt : 1);
  $('#cfgdisk').checked = !!c.disk;
  $('.cfgnow').textContent = !c.url ? 'Not configured yet.'
    : 'Current: ' + (isLocal ? ('Local — ' + c.url) : 'OpenAI')
      + ' · key ' + (c.has_key ? 'saved' : 'not set');
  $('.cfghint').textContent = c.has_key ? 'A key is saved — leave blank to keep it.' : '';
  updateProvUI();
}
function openConfigModal() {
  // Open immediately; fill from the device asynchronously (don't block the UI).
  $('.cfgnow').textContent = 'Loading…';
  $('#cfgmodal').classList.add('open');
  fetch('/api/config').then(r => r.json()).then(fillConfig).catch(() => {});
  return new Promise(res => { cfgResolve = res; });
}
function closeConfig(v) {
  $('#cfgmodal').classList.remove('open');
  if (cfgResolve) { cfgResolve(v); cfgResolve = null; }
}
async function saveConfig() {
  const local = $('#provlocal').checked;
  const url = local ? $('#cfgurl').value.trim() : ''; // blank => OpenAI default on device
  if (local && !url) { notice('Missing URL', 'Enter the local server URL.'); return; }
  const key = $('#cfgkey').value;
  const body = { url };
  if (key) body.key = key; // blank = keep the existing key
  const sleep_s = parseInt($('#cfgsleep').value, 10);
  const hold_s = parseInt($('#cfghold').value, 10);
  if (!Number.isNaN(sleep_s)) body.sleep_s = sleep_s;
  if (!Number.isNaN(hold_s)) body.hold_s = hold_s;
  const scroll = parseInt($('#cfgscroll').value, 10);
  const name_fmt = parseInt($('#cfgname').value, 10);
  if (!Number.isNaN(scroll)) body.scroll = scroll;
  if (!Number.isNaN(name_fmt)) body.name_fmt = name_fmt;
  body.disk = $('#cfgdisk').checked;
  const r = await fetch('/api/config', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  if (!r.ok) { notice('Could not save settings', await r.text()); return; }
  loadInfo();
  closeConfig(true);
}
async function clearConfig() {
  await fetch('/api/config', { method: 'DELETE' });
  loadInfo();
  closeConfig(false);
}

let rnId = null;
function openRename(id, cur) {
  rnId = id;
  $('#rnname').value = cur || '';
  $('#renamemodal').classList.add('open');
  setTimeout(() => $('#rnname').focus(), 50);
}
function closeRename() { rnId = null; $('#renamemodal').classList.remove('open'); }
async function saveRename() {
  if (!rnId) return;
  const name = $('#rnname').value.trim().slice(0, 100);
  const r = await fetch('/api/notes/' + rnId, {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name })
  });
  if (!r.ok) { notice('Rename failed', await r.text()); return; }
  closeRename();
  rows.clear(); $('#list').innerHTML = ''; // filenames changed -> rebuild rows
  load();
}

async function transcribe(id) {
  busy.add(id);
  const row = rows.get(id);
  if (row) { row.classList.add('busy'); const a = $('.act', row); if (a) a.disabled = true; }
  try {
    let r = await fetch('/api/transcribe/' + id, { method:'POST' });
    if (r.status === 428) { // device needs a transcription service configured
      if (await openConfigModal()) r = await fetch('/api/transcribe/' + id, { method:'POST' });
      else return;
    }
    if (!r.ok) notice('Transcription failed', await r.text());
    else { open.add(id); if (row) row.dataset.txLoaded = ''; }
  } catch (e) { notice('Transcription error', String(e)); }
  finally { busy.delete(id); load(); }
}

function toggleTx(id, row) {
  if (open.has(id)) open.delete(id); else open.add(id);
  $('.tx', row).classList.toggle('open', open.has(id));
  if (open.has(id)) fillTx(id, row);
}
async function fillTx(id, row) {
  const pre = $('.tx pre', row);
  try { pre.textContent = (await (await fetch('/api/transcript/' + id)).text()) || '(empty)'; row.dataset.txLoaded = '1'; }
  catch (e) { pre.textContent = '(failed to load)'; }
}

// --- delete with confirmation (same prompt as the simulator) ---------------
let delId = null;
function confirmDelete(id, title) {
  delId = id;
  $('#modal .mtitle').textContent = title || 'Untitled';
  $('#modal').classList.add('open');
}
function closeModal() { delId = null; $('#modal').classList.remove('open'); }
async function doDelete() {
  if (!delId) return;
  const id = delId; closeModal();
  try {
    const r = await fetch('/api/notes/' + id, { method: 'DELETE' });
    if (!r.ok) notice('Delete failed', await r.text());
  } catch (e) { notice('Delete error', String(e)); }
  open.delete(id); busy.delete(id);
  load();
}

// --- pretty notice / error dialog ------------------------------------------
function notice(title, msg) {
  $('#notice .ntitle').textContent = title;
  $('#notice .nmsg').textContent = msg;
  let hint = '';
  if (/SAPIENS_WHISPER_MODEL/.test(msg))
    hint = 'No local model is set. Start sapiens-web with a model — ' +
           'SAPIENS_WHISPER_MODEL=/path/ggml-base.bin — or use --engine openai ' +
           '(with OPENAI_API_KEY), or --engine fake to try it out.';
  else if (/OPENAI_API_KEY/.test(msg))
    hint = 'Set OPENAI_API_KEY before starting sapiens-web --engine openai.';
  const h = $('#notice .nhint'); h.textContent = hint; h.style.display = hint ? '' : 'none';
  $('#notice').classList.add('open');
}
$('#notice .nok').onclick = () => $('#notice').classList.remove('open');
$('#notice').onclick = (e) => { if (e.target.id === 'notice') e.currentTarget.classList.remove('open'); };

// --- re-transcribe confirmation (when a transcript already exists) ----------
let reId = null;
function openReConfirm(id, title) {
  reId = id;
  $('#remodal .rtitle').textContent = title || 'Untitled';
  $('#remodal').classList.add('open');
}
function closeRe() { reId = null; $('#remodal').classList.remove('open'); }
$('#remodal .rredo').onclick = () => { const id = reId; closeRe(); if (id) transcribe(id); };
$('#remodal .rcancel').onclick = closeRe;
$('#remodal').onclick = (e) => { if (e.target.id === 'remodal') closeRe(); };

// --- sound spectrum (Web Audio) shown in the playing tile ------------------
let actx = null;
const ANALYSERS = new WeakMap(); // audio element -> AnalyserNode (created once)
let viz = { row: null, an: null, raf: 0 };
function analyserFor(au) {
  if (!actx) actx = new (window.AudioContext || window.webkitAudioContext)();
  if (!ANALYSERS.has(au)) {
    const src = actx.createMediaElementSource(au);
    const an = actx.createAnalyser(); an.fftSize = 256; an.smoothingTimeConstant = 0.8;
    src.connect(an); an.connect(actx.destination);
    ANALYSERS.set(au, an);
  }
  return ANALYSERS.get(au);
}
function startViz(row, au) {
  try { if (actx) actx.resume(); viz.an = analyserFor(au); }
  catch (e) { return; } // Web Audio unavailable -> skip the spectrum gracefully
  viz.row = row;
  if (!viz.raf) drawViz();
}
function stopViz(row) {
  if (viz.row !== row) return;
  if (viz.raf) { cancelAnimationFrame(viz.raf); viz.raf = 0; }
  const cv = $('.spec', row);
  if (cv) cv.getContext('2d').clearRect(0, 0, cv.width, cv.height);
  viz.row = null; viz.an = null;
}
function drawViz() {
  viz.raf = requestAnimationFrame(drawViz);
  if (!viz.row || !viz.an) return;
  const cv = $('.spec', viz.row); if (!cv) return;
  const n = viz.an.frequencyBinCount, data = new Uint8Array(n);
  viz.an.getByteFrequencyData(data);
  const ctx = cv.getContext('2d'), W = cv.width, H = cv.height;
  ctx.clearRect(0, 0, W, H);
  ctx.fillStyle = '#000';
  // Voice energy lives in the lower bins — map just those across the FULL width
  // so the box fills with many bars instead of a half-empty plot.
  const usable = Math.max(8, Math.floor(n * 0.30)); // active voice band only
  const bars = Math.max(20, Math.floor(W / 2.2));   // many bars across the box
  const bw = W / bars;
  for (let i = 0; i < bars; i++) {
    const idx = Math.min(usable - 1, Math.floor((i / bars) * usable));
    const h = Math.min(H, (data[idx] / 255) * H * 1.7); // gain so bars reach up
    ctx.fillRect(i * bw, H - h, Math.max(1, bw - 1), Math.max(1, h));
  }
}
$('#modal .mdel').onclick = doDelete;
$('#modal .mcancel').onclick = closeModal;
$('#modal').onclick = (e) => { if (e.target.id === 'modal') closeModal(); };
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') { closeModal(); closeRe(); closeConfig(false); closeRename(); $('#notice').classList.remove('open'); }
});

// Transcription-settings modal wiring.
$('#cfgbtn').onclick = () => openConfigModal();
$('#provoa').onchange = updateProvUI;
$('#provlocal').onchange = updateProvUI;
$('#cfgmodal .csave').onclick = saveConfig;
$('#cfgmodal .cclear').onclick = clearConfig;
$('#cfgmodal .ccancel').onclick = () => closeConfig(false);
$('#cfgmodal').onclick = (e) => { if (e.target.id === 'cfgmodal') closeConfig(false); };
$('#renamemodal .rnsave').onclick = saveRename;
$('#renamemodal .rncancel').onclick = closeRename;
$('#renamemodal').onclick = (e) => { if (e.target.id === 'renamemodal') closeRename(); };
$('#rnname').addEventListener('keydown', e => { if (e.key === 'Enter') saveRename(); });

function loadInfo() {
  fetch('/api/info').then(r=>r.json()).then(i=>{
    $('#engine').textContent = 'engine: ' + i.engine;
    if (i.version) $('#ver').textContent = 'v' + i.version;
  }).catch(()=>{});
}
loadInfo();
load();
setInterval(load, 4000);
</script>
</body>
</html>
)HTML";

} // namespace sapiens::web
