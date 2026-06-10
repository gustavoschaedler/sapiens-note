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
           box-shadow:5px 6px 0 var(--shadow); padding:18px 20px; width:90%; max-width:360px; }
  .modal h2 { margin:0 0 6px; }
  .modal .mtitle { font-weight:700; margin:6px 0; overflow:hidden;
                   text-overflow:ellipsis; white-space:nowrap; }
  .modal .mbtns { display:flex; gap:12px; justify-content:flex-end; margin-top:14px; }
  .modal pre { white-space:pre-wrap; word-break:break-word; margin:8px 0;
               font-family: ui-monospace, Menlo, Consolas, monospace; font-size:13px;
               background:#f2f2f2; border:1px solid var(--ink); border-radius:6px;
               padding:8px; max-height:160px; overflow:auto; }
  .modal .hint { color:var(--muted); font-size:13px; margin:6px 0 0; }
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
  <span class="sub" id="sub">recordings</span>
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

async function transcribe(id) {
  busy.add(id);
  const row = rows.get(id);
  if (row) { row.classList.add('busy'); const a = $('.act', row); if (a) a.disabled = true; }
  try {
    const r = await fetch('/api/transcribe/' + id, { method:'POST' });
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
  if (e.key === 'Escape') { closeModal(); closeRe(); $('#notice').classList.remove('open'); }
});

fetch('/api/info').then(r=>r.json()).then(i=>{
  $('#engine').textContent = 'engine: ' + i.engine;
  if (i.version) $('#ver').textContent = 'v' + i.version;
}).catch(()=>{});
load();
setInterval(load, 4000);
</script>
</body>
</html>
)HTML";

} // namespace sapiens::web
