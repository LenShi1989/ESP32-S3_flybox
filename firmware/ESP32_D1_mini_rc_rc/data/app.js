'use strict';

const $ = (id) => document.getElementById(id);
let TOKEN = localStorage.getItem('kkbox-rc-token') || '';
let ws = null;

/* ---------- API ---------- */
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
  let data; try { data = JSON.parse(txt); } catch (e) { data = txt; }
  if (!r.ok) throw new Error((data && data.err) || ('HTTP ' + r.status));
  return data;
}

let toastTimer = null;
function toast(msg, kind = 'ok') {
  const t = $('toast'); t.textContent = msg; t.className = 'toast ' + kind;
  clearTimeout(toastTimer); toastTimer = setTimeout(() => t.classList.add('hidden'), 2600);
}

/* ---------- 彈窗 ---------- */
function showModal(title, msg) {
  $('modal-title').textContent = title;
  $('modal-msg').textContent = msg;
  $('modal').classList.remove('hidden');
}
$('modal-ok').addEventListener('click', () => $('modal').classList.add('hidden'));

/* ---------- 主題切換 (明亮 / 暗黑 / 玻璃) ---------- */
const THEME_KEY = 'kkbox-rc-theme';
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
  if ($('page-joy').classList.contains('active')) setKnob(joyRoll / RANGE, -joyPitch / RANGE);
});
setNav(false);

/* ---------- 登入 ---------- */
function showLogin() {
  $('app').classList.add('hidden');
  $('login').classList.remove('hidden');
  if (ws) { try { ws.close(); } catch (e) {} ws = null; }
}
$('login-form').addEventListener('submit', async (e) => {
  e.preventDefault(); $('lg-err').textContent = '';
  try {
    const d = await fetch('/api/login', { method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ user: $('lg-user').value, pass: $('lg-pass').value }).toString() });
    const j = await d.json();
    if (!d.ok) { $('lg-err').textContent = j.err || '登入失敗'; return; }
    TOKEN = j.token || ''; localStorage.setItem('kkbox-rc-token', TOKEN); start();
  } catch (e) { $('lg-err').textContent = '無法連線'; }
});
$('logout').addEventListener('click', async () => {
  try { await api('/api/logout', { method: 'POST' }); } catch (e) {}
  localStorage.removeItem('kkbox-rc-token'); TOKEN = ''; showLogin();
});

/* ---------- 導覽 ---------- */
$('nav').addEventListener('click', (e) => {
  const a = e.target.closest('a[data-page]'); if (!a) return;
  document.querySelectorAll('#nav a').forEach(x => x.classList.toggle('active', x === a));
  document.querySelectorAll('.page').forEach(p => p.classList.toggle('active', p.id === 'page-' + a.dataset.page));
  if (a.dataset.page === 'network') loadNet();
  if (a.dataset.page === 'hardware') loadHw();
  if (a.dataset.page === 'pid') loadPid();
  if (a.dataset.page === 'user') loadUser();
  if (a.dataset.page === 'esc') { loadEsc(); loadMmin(); }
  /* 頁面切換後才取得實際尺寸，避免搖桿圓點停在左上角 */
  if (a.dataset.page === 'joy') setKnob(joyRoll / RANGE, -joyPitch / RANGE);
});

/* ---------- 狀態 ---------- */
function fmtUptime(s) {
  const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600),
        m = Math.floor(s % 3600 / 60), ss = s % 60;
  return (d ? d + 'd ' : '') + h + 'h ' + m + 'm ' + ss + 's';
}
async function loadStatus() {
  let s; try { s = await api('/api/status'); } catch (e) { return; }
  $('s-fw').textContent = s.fw + ' ' + s.ver;
  $('s-uptime').textContent = fmtUptime(s.uptime);
  $('s-heap').textContent = (s.heap / 1024).toFixed(1) + ' KB';
  const linked = !!s.link.up;
  const up = linked && s.link.age < 2000;
  $('s-link').textContent = 'WiFi：' + (linked ? (up ? '已連線' : '連線中') : '未連線');
  $('s-link').style.color = up ? 'var(--ok)' : 'var(--muted)';
  $('net-dot').classList.toggle('on', s.wifi.connected || linked);
  const srcNames = ['自動', '硬體', '軟體'];
  $('s-id').textContent = s.id ? (s.id.drone + ' / RC ' + s.id.rc) : '-';
  $('s-ctrl').textContent = s.control
    ? (srcNames[s.control.source] || '-') + '（目前：' + (srcNames[s.control.active] || '-') + '）'
    : '-';
  $('s-page').textContent = ['飛行遙測', '硬體輸入', '網路資訊', 'ID / 校正'][s.page] || ('第 ' + (s.page + 1) + ' 頁');

  $('s-wifi-c').textContent = s.wifi.connected ? '已連線' : '未連線';
  $('s-wifi-c').style.color = s.wifi.connected ? 'var(--ok)' : 'var(--muted)';
  $('s-wifi-ssid').textContent = s.wifi.ssid || '-';
  $('s-wifi-ip').textContent = s.wifi.ip;
  $('s-wifi-rssi').textContent = s.wifi.connected ? s.wifi.rssi + ' dBm' : '-';

  if (s.ap && s.ap.active) $('s-ap').textContent = s.ap.ssid + ' (' + s.ap.ip + ')';
  else $('s-ap').textContent = '-';

  const st = $('joy-state');
  if (linked && up) { $('joy-state').textContent = '已連線接收機'; }
  else if (linked) { $('joy-state').textContent = '連線中...'; }
  else $('joy-state').textContent = '未連線';

  /* 飛控最後回應: 走 HTTP 輪詢, 瀏覽器 WS 中斷時仍可確認命令結果 */
  if ((s.kkAck || '') !== lastAckPoll) {
    lastAckPoll = s.kkAck || '';
    const e = $('kk-ack');
    if (e) e.textContent = s.kkAck || '-';
    try {
      const a = JSON.parse(s.kkAck);
      if (a && a.mmin) showMmin(a.mmin);
      if (a && a.ack) onAck(a.ack, 'poll');
    } catch (err) {}
  }
}

function applyTelemetry(t) {
  if (!t || !t.gyro) return;
  armedNow = !!t.armed;
  $('t-armed').textContent = t.armed ? '已解鎖' : '上鎖';
  $('t-armed').style.color = t.armed ? 'var(--warn)' : 'var(--muted)';
  $('t-thr').textContent = t.set ? t.set.thr : '-';
  $('t-gr').textContent = t.gyro.roll.toFixed(1);
  $('t-gp').textContent = t.gyro.pitch.toFixed(1);
  $('t-gy').textContent = t.gyro.yaw.toFixed(1);
  const motorLabels = ['M1 FL CCW', 'M2 FR CW', 'M3 RR CCW', 'M4 RL CW',
                       'M5 FL 下槳 CW', 'M6 FR 下槳 CCW', 'M7 RR 下槳 CW', 'M8 RL 下槳 CCW'];
  $('t-motor').textContent = (t.motor || []).map(function (value, i) {
    return (motorLabels[i] || ('M' + (i + 1))) + ':' + value;
  }).join(' · ');
  $('joy-state').textContent = t.armed ? '已解鎖 (ARMED)' : '已連線 (待命)';
  if (t.hold !== undefined) setHold(!!t.hold);
  loadEsc();
}

async function loadFs() {
  let f; try { f = await api('/api/fs'); } catch (e) { return; }
  const pct = f.total ? (f.used / f.total * 100) : 0;
  $('fs-usage').textContent = (f.used / 1024).toFixed(1) + ' / ' + (f.total / 1024).toFixed(1) + ' KB (' + pct.toFixed(0) + '%)';
  $('fs-bar').style.width = pct.toFixed(1) + '%';
  const tb = $('fs-files').querySelector('tbody'); tb.innerHTML = '';
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
  ws.onopen = () => setWs('已建立 (即時)', true);
  ws.onmessage = (ev) => {
    try {
      const m = JSON.parse(ev.data);
      if (m.gyro) applyTelemetry(m);
      else if (m.mmin) { showMmin(m.mmin); if (m.ack) onAck(m.ack, 'ws'); }
      else if (m.ack) onAck(m.ack, 'ws');
      else if (m.err) onLinkErr(m.err);
      else if (m.evt === 'failsafe') onLinkErr('飛控 failsafe 已自動上鎖');
    } catch (e) {}
  };
  ws.onclose = () => {
    setWs('中斷 (改 REST 送出, 無即時 ack)', false);
    ws = null;
    setTimeout(() => { if (!$('app').classList.contains('hidden')) connectWs(); }, 3000);
  };
}
function setWs(txt, ok) {
  const e = $('ws-state');
  if (!e) return;
  e.textContent = txt;
  e.style.color = ok ? 'var(--ok)' : 'var(--warn)';
}
let txCount = 0;
function sendCmd(obj) {
  const s = JSON.stringify(obj);
  txCount++;
  const e = $('tx-info');
  if (e) e.textContent = txCount + ' 筆 (最後: ' + (obj.cmd || '?') + ')';
  if (ws && ws.readyState === 1) ws.send(s);
  else api('/api/command', { method: 'POST', body: { cmd: s } }).catch((e) => onLinkErr(e.message));
}

/* ---------- 命令回應追蹤: KK 板遙測傳輸中 RX 短暫全盲, 單次命令需重送 ---------- */
const pendingCmd = {};
let lastErrAt = 0;
let lastAckPoll = '';
function onAck(cmd, src) {
  if (cmd === 'motmin' && mminPending > 0) {
    /* WS 已即時收到時, HTTP 輪詢的舊 ack 不重複計數 */
    if (src === 'poll' && ws && ws.readyState === 1) return;
    mminPending--;
    if (mminPending === 0) $('mmin-state').textContent = '飛控已確認 8 顆設定';
    return;
  }
  const st = pendingCmd[cmd];
  if (!st) return;   /* 週期性命令 (throttle/setpoints) 的回應不提示 */
  clearTimeout(st.timer); delete pendingCmd[cmd];
  toast('✓ ' + cmd + ' 飛控已回應', 'ok');
}
function onLinkErr(msg) {
  const now = Date.now();
  if (now - lastErrAt < 3000) return;   /* 節流: 週期命令失敗不洗版 */
  lastErrAt = now;
  toast('✗ ' + msg, 'err');
}
function sendOnce(obj, tries = 3) {
  const cmd = obj.cmd;
  const old = pendingCmd[cmd];
  if (old) clearTimeout(old.timer);
  const st = { n: 0, timer: 0 };
  pendingCmd[cmd] = st;
  (function go() {
    sendCmd(obj);
    st.n++;
    st.timer = setTimeout(() => {
      if (st.n < tries) { go(); return; }
      if (pendingCmd[cmd] === st) delete pendingCmd[cmd];
      toast('✗ ' + cmd + ' 無回應 (' + tries + ' 次)', 'err');
    }, 250);
  })();
}

/* ---------- 網路設定 ---------- */
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

async function loadNet() {
  let l; try { l = await api('/api/link'); } catch (e) { return; }
  $('lk-host').value = l.host; $('lk-port').value = l.port; $('lk-token').value = l.token;
}
$('lk-save').addEventListener('click', async () => {
  try {
    await api('/api/link', { method: 'POST', body: { host: $('lk-host').value, port: $('lk-port').value, token: $('lk-token').value } });
    toast('已儲存，重新連線接收機');
  } catch (e) { toast('儲存失敗', 'err'); }
});

/* ---------- PID ---------- */
async function loadPid() {
  let p;
  try { p = await api('/api/pid'); } catch (e) { return; }
  for (const ax of ['roll', 'pitch', 'yaw']) {
    $('pid-' + ax + '-kp').value = p[ax].kp;
    $('pid-' + ax + '-ki').value = p[ax].ki;
    $('pid-' + ax + '-kd').value = p[ax].kd;
  }
}
$('pid-save').addEventListener('click', async () => {
  try {
    for (const ax of ['roll', 'pitch', 'yaw']) {
      await api('/api/pid', { method: 'POST', body: {
        axis: ax, kp: $('pid-' + ax + '-kp').value,
        ki: $('pid-' + ax + '-ki').value, kd: $('pid-' + ax + '-kd').value } });
    }
    toast('PID 已儲存並下傳飛控板');
  } catch (e) { toast('儲存失敗: ' + e.message, 'err'); }
});

/* ---------- 硬體遙控 ---------- */
async function loadHw() {
  let h; try { h = await api('/api/hardware'); } catch (e) { return; }
  $('hw-raw-t').textContent = h.raw.t; $('hw-out-t').textContent = h.out.thr + ' us';
  $('hw-raw-y').textContent = h.raw.y; $('hw-out-y').textContent = h.out.yaw.toFixed(0);
  $('hw-raw-p').textContent = h.raw.p; $('hw-out-p').textContent = h.out.pitch.toFixed(0);
  $('hw-raw-r').textContent = h.raw.r; $('hw-out-r').textContent = h.out.roll.toFixed(0);
  $('hw-raw-v').textContent = h.raw.vrc1;
  $('hw-out-v').textContent = 'ID ' + h.id + ' / 第 ' + (h.page + 1) + ' 頁';
  $('hw-s1').textContent = h.sw.s1 ? 'ON' : 'OFF';
  $('hw-s2').textContent = h.sw.s2 ? 'ON' : 'OFF';
  $('hw-s3').textContent = h.sw.s3 ? 'ON' : 'OFF';
  $('hw-s4').textContent = h.sw.s4 ? 'ON' : 'OFF';
  if (h.fly !== undefined && document.activeElement !== $('hw-fly')) $('hw-fly').value = h.fly;
  $('hw-source').value = String(h.source);
  const cal = h.cal || {};
  $('hw-cal-state').textContent = (cal.valid ? '已校正' : '未校正') + (cal.active ? '（校正中…）' : '');
  $('hw-cal-tmin').textContent = cal.tmin; $('hw-cal-tmax').textContent = cal.tmax;
  $('hw-cal-ymin').textContent = cal.ymin; $('hw-cal-ymax').textContent = cal.ymax;
  $('hw-cal-pmin').textContent = cal.pmin; $('hw-cal-pmax').textContent = cal.pmax;
  $('hw-cal-rmin').textContent = cal.rmin; $('hw-cal-rmax').textContent = cal.rmax;
}

$('hw-id-save').addEventListener('click', async () => {
  try {
    await api('/api/id', { method: 'POST', body: { drone: $('hw-fly').value } });
    toast('無人機 ID 已儲存'); loadHw(); loadStatus();
  } catch (e) { toast('儲存失敗: ' + e.message, 'err'); }
});
$('hw-source-save').addEventListener('click', async () => {
  try {
    await api('/api/control', { method: 'POST', body: { source: $('hw-source').value } });
    toast('控制來源已套用'); loadHw(); loadStatus();
  } catch (e) { toast('套用失敗: ' + e.message, 'err'); }
});
$('hw-cal-start').addEventListener('click', async () => {
  try { await api('/api/cal', { method: 'POST', body: { action: 'start' } }); toast('校正開始，請推動四軸到兩端'); loadHw(); }
  catch (e) { toast('校正失敗: ' + e.message, 'err'); }
});
$('hw-cal-stop').addEventListener('click', async () => {
  try {
    const r = await api('/api/cal', { method: 'POST', body: { action: 'stop' } });
    toast(r.valid ? '校正完成並已儲存' : '行程不足，已保留舊校正值', r.valid ? 'ok' : 'err');
    loadHw();
  } catch (e) { toast('校正失敗: ' + e.message, 'err'); }
});
/* ---------- 使用者 ---------- */
async function loadUser() {
  let u; try { u = await api('/api/user'); } catch (e) { return; }
  $('u-user').value = u.user;
}
$('u-save').addEventListener('click', async () => {
  try {
    await api('/api/user', { method: 'POST', body: { user: $('u-user').value, pass: $('u-pass').value, cur: $('u-cur').value } });
    toast('已儲存'); $('u-pass').value = ''; $('u-cur').value = '';
  } catch (e) { toast('儲存失敗: ' + e.message, 'err'); }
});

/* ---------- 模擬搖桿 ---------- */
const RANGE = 200;          /* 角速率滿刻度 deg/s */
let joyRoll = 0, joyPitch = 0, joyYaw = 0, joyThr = 1000, dirty = false;
let armedNow = false;
let holdOn = false;
const pad = $('pad'), knob = $('knob');
let dragging = false;

function setKnob(nx, ny) {
  const r = pad.clientWidth * 0.5 - 28;
  knob.style.left = (pad.clientWidth / 2 + nx * r) + 'px';
  knob.style.top  = (pad.clientHeight / 2 + ny * r) + 'px';
}
function padMove(clientX, clientY) {
  const rect = pad.getBoundingClientRect();
  let nx = (clientX - rect.left - rect.width / 2) / (rect.width / 2 - 28);
  let ny = (clientY - rect.top - rect.height / 2) / (rect.height / 2 - 28);
  const len = Math.hypot(nx, ny);
  if (len > 1) { nx /= len; ny /= len; }
  setKnob(nx, ny);
  joyRoll  = Math.round(nx * RANGE);
  joyPitch = Math.round(-ny * RANGE);
  $('joy-roll').textContent = joyRoll;
  $('joy-pitch').textContent = joyPitch;
  dirty = true;
}
pad.addEventListener('pointerdown', (e) => { dragging = true; pad.setPointerCapture(e.pointerId); padMove(e.clientX, e.clientY); });
pad.addEventListener('pointermove', (e) => { if (dragging) padMove(e.clientX, e.clientY); });
pad.addEventListener('pointerup',   () => { dragging = false; });
pad.addEventListener('pointercancel', () => { dragging = false; });

function resetPad() {
  joyRoll = 0; joyPitch = 0; joyYaw = 0; joyThr = 1000;
  setKnob(0, 0);
  $('joy-roll').textContent = '0'; $('joy-pitch').textContent = '0';
  $('joy-yaw').value = 0; $('joy-yaw-v').textContent = '0';
  $('joy-thr').value = 1000; $('joy-thr-v').textContent = '1000';
}
$('btn-reset').addEventListener('click', () => { resetPad(); dirty = true; });

$('joy-thr').addEventListener('input', (e) => { joyThr = +e.target.value; $('joy-thr-v').textContent = joyThr; dirty = true; });
$('joy-yaw').addEventListener('input', (e) => { joyYaw = +e.target.value; $('joy-yaw-v').textContent = joyYaw; dirty = true; });

$('btn-arm').addEventListener('click', () => sendOnce({ cmd: 'arm' }));
$('btn-disarm').addEventListener('click', () => sendOnce({ cmd: 'disarm' }));
$('btn-cal').addEventListener('click', () => sendOnce({ cmd: 'calibrate' }));

/* 定高 (油門保持): 鎖定飛控板當前油門, 無高度感測器 */
function setHold(on) {
  holdOn = on;
  const b = $('btn-hold');
  if (b) { b.textContent = '定高：' + (on ? '開' : '關'); b.classList.toggle('danger', on); }
}
$('btn-hold').addEventListener('click', () => {
  setHold(!holdOn);
  sendOnce({ cmd: 'hold', on: holdOn ? 1 : 0 });
});
document.querySelectorAll('[data-led]').forEach(b =>
  b.addEventListener('click', () => sendOnce({ cmd: 'led', mode: b.dataset.led })));

/* 連續傳送 (20Hz) */
setInterval(() => {
  if (!dirty) return;
  sendCmd({ cmd: 'throttle', value: joyThr });
  sendCmd({ cmd: 'setpoints', roll: joyRoll, pitch: joyPitch, yaw: joyYaw });
  dirty = false;
}, 50);

/* 解鎖中每 500ms 送 ping 保活: KK 板 2 秒無命令即 failsafe 上鎖,
 * 否則搖桿靜止時 (無 input 事件) 會被誤判失聯而斷馬達 */
setInterval(() => {
  if (armedNow) sendCmd({ cmd: 'ping' });
}, 500);

/* ---------- 馬達校正 (ESC 行程校正) ---------- */
function loadEsc() {
  const e = $('esc-armed');
  if (!e) return;
  e.textContent = armedNow ? '已解鎖' : '上鎖';
  e.style.color = armedNow ? 'var(--warn)' : 'var(--ok)';
}
$('esc-arm').addEventListener('click', () => {
  sendOnce({ cmd: 'nopid', on: 1 });   /* 校正前先關閉 PID */
  sendOnce({ cmd: 'arm' });
});
$('esc-disarm').addEventListener('click', () => {
  sendOnce({ cmd: 'disarm' });
  sendOnce({ cmd: 'nopid', on: 0 });   /* 離開校正, 恢復 PID */
});

/* 固定轉速: 直接寫入 joyThr (與模擬搖桿共用), 由 50ms 迴圈持續送出, 飛控板不做 PID */
$('esc-thr').addEventListener('input', (e) => { $('esc-thr-v').textContent = e.target.value; });
$('esc-apply').addEventListener('click', () => {
  joyThr = +$('esc-thr').value;
  $('joy-thr').value = joyThr; $('joy-thr-v').textContent = joyThr;
  dirty = true;
  sendOnce({ cmd: 'nopid', on: 1 });
  toast('固定轉速 ' + joyThr + 'us 已套用 (PID 已關閉)', 'ok');
});
$('esc-stop').addEventListener('click', () => {
  joyThr = 1000;
  $('esc-thr').value = 1000; $('esc-thr-v').textContent = '1000';
  $('joy-thr').value = 1000; $('joy-thr-v').textContent = '1000';
  dirty = true;
  toast('已停止輸出 (1000us)', 'ok');
});

/* ---------- 各馬達最低啟動 PWM ---------- */
let mminPending = 0;
function showMmin(arr) {
  if (!arr || !arr.length) return;
  for (let i = 0; i < 8; i++) {
    const el = $('mmin-' + (i + 1));
    if (el && arr[i] !== undefined) el.value = arr[i];
  }
  $('mmin-state').textContent = '已讀取飛控值（已存於 EEPROM）';
}
function mminVal(i) {
  const el = $('mmin-' + i);
  let v = parseInt(el.value, 10);
  if (!isFinite(v)) v = 1000;
  v = Math.max(1000, Math.min(1500, v));
  el.value = v;
  return v;
}
function loadMmin() {
  $('mmin-state').textContent = '讀取中...';
  sendCmd({ cmd: 'status' });
  setTimeout(() => {
    if ($('mmin-state').textContent === '讀取中...')
      $('mmin-state').textContent = '未收到飛控回應（請確認連線）';
  }, 1500);
}
$('mmin-load').addEventListener('click', loadMmin);
$('mmin-save').addEventListener('click', () => {
  mminPending = 8;
  for (let i = 1; i <= 8; i++) sendCmd({ cmd: 'motmin', index: i, value: mminVal(i) });
  $('mmin-state').textContent = '已送出，等待飛控確認...';
  toast('已送出各馬達最低啟動 PWM', 'ok');
  setTimeout(() => {
    if (mminPending > 0)
      $('mmin-state').textContent = '已送出，飛控確認 ' + (8 - mminPending) + '/8';
  }, 2000);
});

/* ---------- 啟動 ---------- */
function start() {
  $('login').classList.add('hidden');
  $('app').classList.remove('hidden');
  resetPad();
  loadStatus(); loadFs(); loadHw(); connectWs();
  setInterval(loadStatus, 3000);
  setInterval(loadFs, 15000);
  setInterval(loadHw, 1000);
}

(async function init() {
  try {
    const ok = await fetch('/api/status', { credentials: 'same-origin', headers: TOKEN ? { 'X-Auth': TOKEN } : {} });
    if (ok.ok) start(); else showLogin();
  } catch (e) { showLogin(); }
})();
