'use strict';

const $  = (id) => document.getElementById(id);
let TOKEN = localStorage.getItem('flybox-token') || '';
let ws = null;

/* ---------- 通用 API ---------- */
async function api(path, opts = {}) {
  const o = { method: opts.method || 'GET', credentials: 'same-origin', headers: {} };
  if (TOKEN) o.headers['X-Auth'] = TOKEN;
  if (opts.body) {
    o.headers['Content-Type'] = 'application/x-www-form-urlencoded';
    o.body = new URLSearchParams(opts.body).toString();
  }
  const r = await fetch(path, o);
  if (r.status === 401) { showLogin(); throw new Error('unauthorized'); }
  const txt = await r.text();
  let data = null;
  try { data = JSON.parse(txt); } catch (e) { data = txt; }
  if (!r.ok) throw new Error((data && data.err) || ('HTTP ' + r.status));
  return data;
}

/* ---------- Toast ---------- */
let toastTimer = null;
function toast(msg, kind = 'ok') {
  const t = $('toast');
  t.textContent = msg;
  t.className = 'toast ' + kind;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.add('hidden'), 2600);
}

/* ---------- 彈窗 ---------- */
function showModal(title, msg) {
  $('modal-title').textContent = title;
  $('modal-msg').textContent = msg;
  $('modal').classList.remove('hidden');
}
$('modal-ok').addEventListener('click', () => $('modal').classList.add('hidden'));

/* ---------- 主題切換 (明亮 / 暗黑 / 玻璃) ---------- */
const THEME_KEY = 'flybox-theme';
function setTheme(t) {
  document.documentElement.dataset.theme = t;
  try { localStorage.setItem(THEME_KEY, t); } catch (e) {}
  document.querySelectorAll('.theme-btn').forEach(b =>
    b.classList.toggle('active', b.dataset.theme === t));
}
document.querySelectorAll('.theme-btn').forEach(b =>
  b.addEventListener('click', () => setTheme(b.dataset.theme)));
setTheme((function () { try { return localStorage.getItem(THEME_KEY) || 'dark'; } catch (e) { return 'dark'; } })());

/* ---------- Sidebar (漢堡選單) ---------- */
const menuBtn = $('menu-toggle');
const backdrop = $('backdrop');
const isDesktop = () => window.matchMedia('(min-width:1025px)').matches;
let wasDesktop = isDesktop();
function setNav(open) {
  document.body.classList.toggle('nav-open', open);
  menuBtn.setAttribute('aria-expanded', String(isDesktop() ? !open : open));
}
menuBtn.addEventListener('click', () =>
  setNav(!document.body.classList.contains('nav-open')));
backdrop.addEventListener('click', () => setNav(false));
$('nav').addEventListener('click', () => { if (!isDesktop()) setNav(false); });
window.addEventListener('resize', () => {
  const d = isDesktop();
  if (d !== wasDesktop) { wasDesktop = d; setNav(false); }
});
setNav(false);

/* ---------- 登入 ---------- */
function showLogin() {
  $('app').classList.add('hidden');
  $('login').classList.remove('hidden');
  if (ws) { try { ws.close(); } catch (e) {} ws = null; }
}

$('login-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  $('lg-err').textContent = '';
  try {
    const d = await fetch('/api/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ user: $('lg-user').value, pass: $('lg-pass').value }).toString()
    });
    const j = await d.json();
    if (!d.ok) { $('lg-err').textContent = j.err || '登入失敗'; return; }
    TOKEN = j.token || '';
    localStorage.setItem('flybox-token', TOKEN);
    start();
  } catch (err) {
    $('lg-err').textContent = '無法連線';
  }
});

$('logout').addEventListener('click', async () => {
  try { await api('/api/logout', { method: 'POST' }); } catch (e) {}
  localStorage.removeItem('flybox-token');
  TOKEN = '';
  showLogin();
});

/* ---------- 導覽 ---------- */
$('nav').addEventListener('click', (e) => {
  const a = e.target.closest('a[data-page]');
  if (!a) return;
  document.querySelectorAll('#nav a').forEach(x => x.classList.toggle('active', x === a));
  document.querySelectorAll('.page').forEach(p => p.classList.toggle('active', p.id === 'page-' + a.dataset.page));
  if (a.dataset.page === 'wifi') loadWifi();
  if (a.dataset.page === 'user') loadUser();
});

/* ---------- 狀態 ---------- */
function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600),
        m = Math.floor(s % 3600 / 60), ss = s % 60;
  return (d ? d + 'd ' : '') + h + 'h ' + m + 'm ' + ss + 's';
}

async function loadStatus() {
  let s;
  try { s = await api('/api/status'); } catch (e) { return; }
  $('s-fw').textContent = s.fw;
  $('s-ver').textContent = s.ver;
  $('fw-ver').textContent = s.ver;
  $('s-uptime').textContent = fmtUptime(s.uptime);
  $('s-heap').textContent = (s.heap / 1024).toFixed(1) + ' KB';
  $('s-drone-id').textContent = s.droneId;

  $('s-wifi-c').textContent = s.wifi.connected ? '已連線' : '未連線';
  $('s-wifi-c').style.color = s.wifi.connected ? 'var(--ok)' : 'var(--muted)';
  $('s-wifi-ssid').textContent = s.wifi.ssid || '-';
  $('s-wifi-ip').textContent = s.wifi.ip;
  $('s-wifi-rssi').textContent = s.wifi.connected ? s.wifi.rssi + ' dBm' : '-';

  const ap = s.ap;
  $('s-ap').textContent = (ap && ap.active) ? ap.ssid + ' (' + ap.ip + ')' : '-';
  $('ap-ssid').textContent = ap && ap.active ? ap.ssid : '-';
  $('ap-ip').textContent = ap && ap.active ? ap.ip : '-';
  $('ap-status').textContent = ap && ap.active ? '啟用' : '停用';

  const dot = $('net-dot');
  dot.classList.toggle('on', s.wifi.connected);
}

async function loadFs() {
  let f;
  try { f = await api('/api/fs'); } catch (e) { return; }
  const pct = f.total ? (f.used / f.total * 100) : 0;
  $('fs-usage').textContent = (f.used / 1024).toFixed(1) + ' / ' + (f.total / 1024).toFixed(1) + ' KB (' + pct.toFixed(0) + '%)';
  $('fs-bar').style.width = pct.toFixed(1) + '%';
  const tb = $('fs-files').querySelector('tbody');
  tb.innerHTML = '';
  (f.files || []).forEach(x => {
    const tr = document.createElement('tr');
    tr.innerHTML = '<td>' + x.name + '</td><td>' + x.size + ' B</td>';
    tb.appendChild(tr);
  });
}

/* ---------- WebSocket ---------- */
function connectWs() {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const url = proto + '//' + location.host + '/ws' + (TOKEN ? '?token=' + encodeURIComponent(TOKEN) : '');
  try { ws = new WebSocket(url); } catch (e) { return; }
  ws.onmessage = (ev) => {
    try {
      const m = JSON.parse(ev.data);
      if (m.hello) return;
    } catch (e) {}
  };
  ws.onclose = () => { ws = null; setTimeout(() => { if (!$('app').classList.contains('hidden')) connectWs(); }, 3000); };
}

/* ---------- WiFi 設定 ---------- */
function rssiLevel(rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -78) return 2;
  if (rssi >= -90) return 1;
  return 0;
}
function wifiEmpty(msg) {
  const box = $('wifi-list');
  box.innerHTML = '';
  const p = document.createElement('p');
  p.className = 'wifi-empty';
  p.textContent = msg;
  box.appendChild(p);
}
function markWifiItem(item) {
  $('wifi-list').querySelectorAll('.wifi-item').forEach(x => {
    const on = x === item;
    x.classList.toggle('selected', on);
    x.setAttribute('aria-selected', String(on));
  });
}
function renderWifiList(list) {
  const nets = [];
  const seen = {};
  (list || []).filter(n => n && n.ssid)
    .sort((a, b) => b.rssi - a.rssi)
    .forEach(n => {
      if (seen[n.ssid]) return;
      seen[n.ssid] = true;
      nets.push(n);
    });
  if (!nets.length) { wifiEmpty('找不到 WiFi 網路，請靠近分享器後重新掃描。'); return; }

  const cur = $('s-wifi-ssid').textContent;
  const box = $('wifi-list');
  box.innerHTML = '';
  nets.forEach(n => {
    const item = document.createElement('button');
    item.type = 'button';
    item.className = 'wifi-item';
    item.dataset.ssid = n.ssid;
    item.setAttribute('role', 'option');
    item.setAttribute('aria-selected', 'false');

    const sig = document.createElement('span');
    sig.className = 'wifi-sig';
    sig.dataset.level = String(rssiLevel(n.rssi));
    sig.setAttribute('aria-hidden', 'true');
    for (let i = 0; i < 4; i++) sig.appendChild(document.createElement('i'));

    const main = document.createElement('span');
    main.className = 'wifi-main';
    const name = document.createElement('span');
    name.className = 'wifi-name';
    name.textContent = n.ssid;
    name.title = n.ssid;
    const meta = document.createElement('span');
    meta.className = 'wifi-meta';
    let mt = n.enc ? '加密' : '開放';
    if (cur && cur !== '-' && n.ssid === cur) { mt += ' · 目前連線'; item.classList.add('is-current'); }
    meta.textContent = mt;
    main.appendChild(name);
    main.appendChild(meta);

    const rssi = document.createElement('span');
    rssi.className = 'wifi-rssi';
    rssi.textContent = n.rssi + ' dBm';

    item.appendChild(sig);
    item.appendChild(main);
    item.appendChild(rssi);
    item.addEventListener('click', () => {
      markWifiItem(item);
      $('wifi-ssid').value = n.ssid;
      $('wifi-scan-state').textContent = '已選擇「' + n.ssid + '」' + (n.enc ? '，請輸入密碼' : '（開放網路）');
      if (n.enc) $('wifi-pass').focus();
    });
    box.appendChild(item);
  });
}
let wifiScanning = false;
$('wifi-scan').addEventListener('click', async () => {
  if (wifiScanning) return;
  wifiScanning = true;
  const btn = $('wifi-scan');
  btn.disabled = true;
  btn.textContent = '掃描中…';
  $('wifi-scan-state').textContent = '正在掃描，請稍候…';
  try {
    renderWifiList(await api('/api/scan'));
    $('wifi-scan-state').textContent = $('wifi-list').querySelectorAll('.wifi-item').length + ' 個網路';
  } catch (e) {
    wifiEmpty('掃描失敗，請稍後再試。');
    $('wifi-scan-state').textContent = '掃描失敗';
  } finally {
    wifiScanning = false;
    btn.disabled = false;
    btn.textContent = '重新掃描';
  }
});
$('wifi-list').addEventListener('keydown', (e) => {
  if (e.key !== 'ArrowDown' && e.key !== 'ArrowUp') return;
  const items = Array.prototype.slice.call($('wifi-list').querySelectorAll('.wifi-item'));
  if (!items.length) return;
  e.preventDefault();
  const at = items.indexOf(document.activeElement);
  const next = e.key === 'ArrowDown' ? Math.min(items.length - 1, at + 1) : Math.max(0, at - 1);
  items[next].focus();
});
$('wifi-ssid').addEventListener('input', () => {
  const v = $('wifi-ssid').value.trim();
  $('wifi-list').querySelectorAll('.wifi-item').forEach(x => {
    const on = x.dataset.ssid === v;
    x.classList.toggle('selected', on);
    x.setAttribute('aria-selected', String(on));
  });
});
$('wifi-save').addEventListener('click', async () => {
  const sel = $('wifi-list').querySelector('.wifi-item.selected');
  const ssid = $('wifi-ssid').value.trim() || (sel ? sel.dataset.ssid : '');
  if (!ssid) { toast('請輸入或選擇 SSID', 'err'); return; }
  $('wifi-scan-state').textContent = '連線中…';
  toast('連線中，請稍候…');
  try {
    const res = await api('/api/wifi', { method: 'POST', body: { ssid, pass: $('wifi-pass').value } });
    const msg = '已連線，IP 位置：' + res.ip + '（系統將重新啟動）';
    $('wifi-scan-state').textContent = msg;
    toast(msg, 'ok');
  } catch (e) { toast('連線失敗: ' + e.message, 'err'); }
});
$('wifi-clear').addEventListener('click', async () => {
  try {
    await api('/api/wifi', { method: 'POST', body: { action: 'clear' } });
    showModal('已刪除', '連線設定已刪除，系統將重新啟動…');
  } catch (e) { toast('清除失敗', 'err'); }
});

/* ---------- 使用者 ---------- */
async function loadUser() {
  let u;
  try { u = await api('/api/user'); } catch (e) { return; }
  $('u-user').value = u.user;
  $('u-token').textContent = u.token;
  try { const idr = await api('/api/id'); $('u-id').value = idr.id; } catch (e) {}
}
$('u-copy').addEventListener('click', () => {
  navigator.clipboard.writeText($('u-token').textContent).then(() => toast('已複製 Token'));
});
$('u-id-save').addEventListener('click', async () => {
  const v = Math.max(0, Math.min(255, parseInt($('u-id').value, 10) || 0));
  try { await api('/api/id', { method: 'POST', body: { id: v } }); $('u-id').value = v; toast('無人機 ID 已儲存'); loadStatus(); }
  catch (e) { toast('儲存失敗: ' + e.message, 'err'); }
});
$('u-save').addEventListener('click', async () => {
  try {
    await api('/api/user', { method: 'POST', body: { user: $('u-user').value, pass: $('u-pass').value, cur: $('u-cur').value } });
    toast('已儲存');
    $('u-pass').value = ''; $('u-cur').value = '';
  } catch (e) { toast('儲存失敗: ' + e.message, 'err'); }
});

/* ---------- 啟動 ---------- */
function start() {
  $('login').classList.add('hidden');
  $('app').classList.remove('hidden');
  loadStatus();
  loadFs();
  connectWs();
  setInterval(loadStatus, 3000);
  setInterval(loadFs, 15000);
}

(async function init() {
  try {
    const ok = await fetch('/api/status', { credentials: 'same-origin', headers: TOKEN ? { 'X-Auth': TOKEN } : {} });
    if (ok.ok) { start(); } else { showLogin(); }
  } catch (e) { showLogin(); }
})();

/* ---------- OTA 上傳 (firmware.bin / spiffs.bin) ---------- */
(function () {
  function bind(prefix, url, label, warn) {
    const fileEl = $(prefix + '-file');
    const btn = $(prefix + '-upload');
    if (!fileEl || !btn) return;
    const bar = $(prefix + '-bar');
    const state = $(prefix + '-state');

    btn.addEventListener('click', () => {
      const f = fileEl.files[0];
      if (!f) { toast('請先選擇 ' + label, 'err'); return; }
      if (!confirm(warn)) return;
      btn.disabled = true;
      if (bar) bar.style.width = '0%';
      if (state) state.textContent = '準備上傳 ' + f.name + ' (' + f.size + ' bytes)';

      const fd = new FormData();
      fd.append('file', f, f.name);

      const xhr = new XMLHttpRequest();
      xhr.open('POST', url);
      if (TOKEN) xhr.setRequestHeader('X-Auth', TOKEN);
      xhr.upload.onprogress = (e) => {
        if (!e.lengthComputable) return;
        const p = Math.round(e.loaded / e.total * 100);
        if (bar) bar.style.width = p + '%';
        if (state) state.textContent = '上傳中 ' + p + '% (' + e.loaded + ' / ' + e.total + ' bytes)';
      };
      xhr.onload = () => {
        btn.disabled = false;
        if (xhr.status >= 200 && xhr.status < 300) {
          if (bar) bar.style.width = '100%';
          let d = null;
          try { d = JSON.parse(xhr.responseText); } catch (err) {}
          if (state) {
            state.textContent = '完成' + (d && d.bytes ? '（' + d.bytes + ' bytes）' : '') +
              '，裝置重新啟動中…';
          }
          toast('上傳完成，裝置重新啟動中');
        } else {
          if (state) state.textContent = '失敗：' + (xhr.responseText || ('HTTP ' + xhr.status));
          toast('上傳失敗', 'err');
        }
      };
      xhr.onerror = () => {
        btn.disabled = false;
        if (state) state.textContent = '上傳失敗 (連線中斷)';
        toast('上傳失敗', 'err');
      };
      xhr.send(fd);
    });
  }

  bind('fw', '/api/ota/firmware', 'firmware.bin',
       '上傳韌體會覆蓋目前版本（寫入備用分割區），完成後自動重開機。確定繼續？');
  bind('sp', '/api/ota/spiffs', 'spiffs.bin',
       '上傳 SPIFFS 映像會覆蓋目前網頁檔，完成後自動重開機。確定繼續？');
})();