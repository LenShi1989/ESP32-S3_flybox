'use strict';

const $  = (id) => document.getElementById(id);
let TOKEN = localStorage.getItem('kkbox-rx-token') || '';
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
const THEME_KEY = 'kkbox-rx-theme';
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
    localStorage.setItem('kkbox-rx-token', TOKEN);
    start();
  } catch (err) {
    $('lg-err').textContent = '無法連線';
  }
});

$('logout').addEventListener('click', async () => {
  try { await api('/api/logout', { method: 'POST' }); } catch (e) {}
  localStorage.removeItem('kkbox-rx-token');
  TOKEN = '';
  showLogin();
});

/* ---------- 導覽 ---------- */
$('nav').addEventListener('click', (e) => {
  const a = e.target.closest('a[data-page]');
  if (!a) return;
  document.querySelectorAll('#nav a').forEach(x => x.classList.toggle('active', x === a));
  document.querySelectorAll('.page').forEach(p => p.classList.toggle('active', p.id === 'page-' + a.dataset.page));
  if (a.dataset.page === 'network') loadNet();
  if (a.dataset.page === 'motors') { motorsTouched = false; mminLoad(); }
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
  $('s-fw').textContent = s.fw + ' ' + s.ver;
  $('s-uptime').textContent = fmtUptime(s.uptime);
  $('s-heap').textContent = (s.heap / 1024).toFixed(1) + ' KB';
  const alive = s.kk.age < 1500;
  $('s-kk').textContent = alive ? '正常' : '無訊號';
  $('s-kk').style.color = alive ? 'var(--ok)' : 'var(--muted)';

  $('s-eth-link').textContent = s.eth.link ? '已連線' : '未連線';
  $('s-eth-link').style.color = s.eth.link ? 'var(--ok)' : 'var(--muted)';
  $('s-eth-ip').textContent = s.eth.ip;
  $('s-eth-spd').textContent = s.eth.speed ? s.eth.speed + ' Mbps' : '-';

  $('s-wifi-c').textContent = s.wifi.connected ? '已連線' : '未連線';
  $('s-wifi-c').style.color = s.wifi.connected ? 'var(--ok)' : 'var(--muted)';
  $('s-wifi-ssid').textContent = s.wifi.ssid || '-';
  $('s-wifi-ip').textContent = s.wifi.ip;
  $('s-wifi-rssi').textContent = s.wifi.connected ? s.wifi.rssi + ' dBm' : '-';

  const dot = $('net-dot');
  dot.classList.toggle('on', s.eth.link || s.wifi.connected);

  $('s-ap').textContent = (s.ap && s.ap.active) ? s.ap.ssid + ' (' + s.ap.ip + ')' : '-';
  if (s.droneId !== undefined) $('f-id').textContent = s.droneId;
}

function applyTelemetry(t) {
  if (!t || !t.gyro) return;
  $('t-armed').textContent = t.armed ? '已解鎖' : '上鎖';
  $('t-armed').style.color = t.armed ? 'var(--warn)' : 'var(--muted)';
  $('t-thr').textContent = t.set ? t.set.thr : '-';
  $('t-gr').textContent = t.gyro.roll.toFixed(1);
  $('t-gp').textContent = t.gyro.pitch.toFixed(1);
  $('t-gy').textContent = t.gyro.yaw.toFixed(1);
  $('t-motor').textContent = (t.motor || []).map(function (value, i) {
    const labels = ['M1 FL CCW', 'M2 FR CW', 'M3 RR CCW', 'M4 RL CW',
                    'M5 FL 下槳 CW', 'M6 FR 下槳 CCW', 'M7 RR 下槳 CW', 'M8 RL 下槳 CCW'];
    return (labels[i] || ('M' + (i + 1))) + ':' + value;
  }).join(' · ');

  if (t.set) updateGauge(t.set.thr);
  if (t.motor) updatePie(t.motor);
  updateAngleBars(t.att);
  updateSensorPanels(t);
  $('f-armed').textContent = t.armed ? '解鎖' : '上鎖';
  if (t.mcount !== undefined) {
    $('f-mcount').textContent = t.mcount + ' 顆';
    if (!motorsTouched) selectMcount(t.mcount);
  }
  $('f-hold').textContent = t.hold ? '開啟' : '關閉';
}

/* ---------- 飛行狀態 (油門儀表盤 / 馬達圓餅圖) ---------- */
const MOTOR_COLORS = ['#37c2ff', '#3ddc84', '#ffcc4d', '#ff5b5b',
                      '#b06bff', '#4de1c1', '#ff9f43', '#8ad6ff'];
const MOTOR_LABELS = ['M1 FL CCW', 'M2 FR CW', 'M3 RR CCW', 'M4 RL CW',
                      'M5 FL 下槳 CW', 'M6 FR 下槳 CCW', 'M7 RR 下槳 CW', 'M8 RL 下槳 CCW'];

function updateGauge(thr) {
  const v = Math.max(1000, Math.min(2000, Math.round(+thr || 0)));
  const frac = (v - 1000) / 1000;
  const fill = $('gauge-fill');
  if (fill) fill.style.strokeDasharray = (frac * 100).toFixed(1) + ' 100';
  $('gauge-needle').style.transform = 'rotate(' + (-90 + frac * 180).toFixed(1) + 'deg)';
  $('gauge-thr').textContent = v;
}

function updatePie(motors) {
  const pie = $('motor-pie');
  if (!pie || !motors || !motors.length) return;
  let total = 0;
  for (const m of motors) total += m;
  if (!total) total = 1;
  let acc = 0, stops = [], html = '';
  for (let i = 0; i < motors.length; i++) {
    const c = MOTOR_COLORS[i % MOTOR_COLORS.length];
    const a = acc / total * 360, b = (acc + motors[i]) / total * 360;
    stops.push(c + ' ' + a.toFixed(2) + 'deg ' + b.toFixed(2) + 'deg');
    acc += motors[i];
    html += '<li><span class="sw" style="background:' + c + '"></span>' +
            (MOTOR_LABELS[i] || ('M' + (i + 1))) +
            '<span class="mv">' + motors[i] + '</span></li>';
  }
  pie.style.background = 'conic-gradient(' + stops.join(',') + ')';
  $('motor-legend').innerHTML = html;
}

/* ---------- 三軸即時角度橫條圖 (Roll/Pitch ±90°, Yaw 0~360°) ---------- */
const ATT_FS = 90;

function updateAngleBars(a) {
  if (!a) return;
  ['roll', 'pitch'].forEach(function (k) {
    const v = Math.max(-ATT_FS, Math.min(ATT_FS, a[k] || 0));
    const frac = v / ATT_FS;
    const fill = $('angle-bar-' + k), val = $('angle-val-' + k);
    if (!fill) return;
    fill.style.left = (50 + Math.min(0, frac) * 50) + '%';
    fill.style.width = (Math.abs(frac) * 50) + '%';
    fill.style.background = v >= 0 ? 'var(--accent)' : 'var(--warn)';
    if (val) val.textContent = (v >= 0 ? '+' : '') + v.toFixed(0) + '°';
  });

  const yaw = (((+a.yaw || 0) % 360) + 360) % 360;
  const yf = $('angle-bar-yaw'), yv = $('angle-val-yaw');
  if (yf) {
    yf.style.left = '0';
    yf.style.width = (yaw / 360 * 100) + '%';
    yf.style.background = 'var(--accent)';
  }
  if (yv) yv.textContent = (Math.round(yaw) % 360) + '°';
}

/* ---------- 六軸姿態 / 三軸地磁 ---------- */
function updateSensorPanels(t) {
  if (t.att) {
    const fmtSigned = (v) => (v >= 0 ? '+' : '') + (+v).toFixed(0);
    $('att-roll').textContent = fmtSigned(t.att.roll);
    $('att-pitch').textContent = fmtSigned(t.att.pitch);
    const yaw = (((+t.att.yaw || 0) % 360) + 360) % 360;
    $('att-yaw').textContent = yaw.toFixed(0);
    $('mag-heading').textContent = yaw.toFixed(0);
    drawCompass(yaw, t.att.roll, t.att.pitch);
  }
  if (t.acc) {
    $('acc-x').textContent = (t.acc.x / 1000).toFixed(2);
    $('acc-y').textContent = (t.acc.y / 1000).toFixed(2);
    $('acc-z').textContent = (t.acc.z / 1000).toFixed(2);
  }
  if (t.mag) {
    $('mag-x').textContent = (t.mag.x / 10).toFixed(1);
    $('mag-y').textContent = (t.mag.y / 10).toFixed(1);
    $('mag-z').textContent = (t.mag.z / 10).toFixed(1);
  }
  if (t.magok !== undefined) {
    const ok = !!t.magok;
    const el = $('mag-state');
    if (el) {
      el.textContent = ok ? 'BMM150 正常' : 'BMM150 無資料';
      el.className = 'pill ' + (ok ? 'ok' : 'err');
    }
  }
}

/* ---------- 羅盤繪製 ---------- */
function drawCompass(yaw, roll, pitch) {
  const canvas = $('compass');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const w = canvas.width;
  const h = canvas.height;
  const cx = w / 2;
  const cy = h / 2;
  const r = Math.min(cx, cy) - 10;

  ctx.clearRect(0, 0, w, h);

  // 背景圓
  ctx.beginPath();
  ctx.arc(cx, cy, r, 0, 2 * Math.PI);
  ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--panel').trim() || '#181d24';
  ctx.fill();
  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--line').trim() || '#2b3340';
  ctx.lineWidth = 2;
  ctx.stroke();

  // 度數刻度
  ctx.font = '11px system-ui';
  ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--muted').trim() || '#8b97a6';
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  for (let i = 0; i < 360; i += 30) {
    const angle = (i - 90) * Math.PI / 180;
    const isMajor = i % 90 === 0;
    const r1 = r - (isMajor ? 20 : 12);
    const r2 = r - 4;
    const x1 = cx + r1 * Math.cos(angle);
    const y1 = cy + r1 * Math.sin(angle);
    const x2 = cx + r2 * Math.cos(angle);
    const y2 = cy + r2 * Math.sin(angle);
    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);
    ctx.strokeStyle = isMajor ? getComputedStyle(document.documentElement).getPropertyValue('--fg').trim() || '#e6ebf2' : getComputedStyle(document.documentElement).getPropertyValue('--muted').trim() || '#8b97a6';
    ctx.lineWidth = isMajor ? 2 : 1;
    ctx.stroke();

    if (isMajor) {
      const label = i === 0 ? 'N' : i === 90 ? 'E' : i === 180 ? 'S' : 'W';
      const lx = cx + (r - 35) * Math.cos(angle);
      const ly = cy + (r - 35) * Math.sin(angle);
      ctx.fillText(label, lx, ly);
    }
  }

  // 方位角數字標示 (每 45 度)
  ctx.font = '10px system-ui';
  for (let i = 0; i < 360; i += 45) {
    if (i % 90 === 0) continue;
    const angle = (i - 90) * Math.PI / 180;
    const lx = cx + (r - 35) * Math.cos(angle);
    const ly = cy + (r - 35) * Math.sin(angle);
    ctx.fillText(i + '°', lx, ly);
  }

  // 機頭指針 (紅色三角形，指向 yaw 方向)
  const needleAngle = (yaw - 90) * Math.PI / 180;
  const needleLen = r - 15;
  const needleW = 10;
  ctx.beginPath();
  ctx.moveTo(cx + needleLen * Math.cos(needleAngle), cy + needleLen * Math.sin(needleAngle));
  ctx.lineTo(cx + needleW * Math.cos(needleAngle + Math.PI * 0.8), cy + needleW * Math.sin(needleAngle + Math.PI * 0.8));
  ctx.lineTo(cx + needleW * Math.cos(needleAngle - Math.PI * 0.8), cy + needleW * Math.sin(needleAngle - Math.PI * 0.8));
  ctx.closePath();
  ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--err').trim() || '#ff5b5b';
  ctx.fill();
  ctx.strokeStyle = getComputedStyle(document.documentElement).getPropertyValue('--fg').trim() || '#e6ebf2';
  ctx.lineWidth = 1.5;
  ctx.stroke();

  // 中心點
  ctx.beginPath();
  ctx.arc(cx, cy, 6, 0, 2 * Math.PI);
  ctx.fillStyle = getComputedStyle(document.documentElement).getPropertyValue('--fg').trim() || '#e6ebf2';
  ctx.fill();

  // 更新側邊資訊
  const ch = $('compass-heading');
  const cr = $('compass-roll');
  const cp = $('compass-pitch');
  if (ch) ch.textContent = Math.round(yaw) + '°';
  if (cr) cr.textContent = (roll >= 0 ? '+' : '') + roll.toFixed(1) + '°';
  if (cp) cp.textContent = (pitch >= 0 ? '+' : '') + pitch.toFixed(1) + '°';
}

let motorsTouched = false;
function selectMcount(n) {
  const el = document.querySelector('input[name=mcount][value="' + n + '"]');
  if (el) el.checked = true;
  $('motors-state').textContent = '目前：' + n + ' 顆';
}
document.querySelectorAll('input[name=mcount]').forEach(r =>
  r.addEventListener('change', () => { motorsTouched = true; }));
$('motors-apply').addEventListener('click', async () => {
  const el = document.querySelector('input[name=mcount]:checked');
  if (!el) { toast('請選擇馬達數', 'err'); return; }
  try {
    await api('/api/command', { method: 'POST', body: { cmd: JSON.stringify({ cmd: 'motors', count: +el.value }) } });
    toast('已送出：' + el.value + ' 顆馬達');
    motorsTouched = false;
  } catch (e) { toast('送出失敗: ' + e.message, 'err'); }
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
function mminSend(obj) {
  return api('/api/command', { method: 'POST', body: { cmd: JSON.stringify(obj) } });
}
async function mminLoad() {
  $('mmin-state').textContent = '讀取中...';
  try { await mminSend({ cmd: 'status' }); }
  catch (e) { $('mmin-state').textContent = '讀取失敗: ' + e.message; return; }
  setTimeout(() => {
    if ($('mmin-state').textContent === '讀取中...')
      $('mmin-state').textContent = '未收到飛控回應（請確認連線）';
  }, 1500);
}
async function mminSave() {
  const btn = $('mmin-save');
  btn.disabled = true;
  $('mmin-state').textContent = '下傳中...';
  try {
    mminPending = 8;
    for (let i = 1; i <= 8; i++) {
      await mminSend({ cmd: 'motmin', index: i, value: mminVal(i) });
    }
    $('mmin-state').textContent = '已送出，等待飛控確認...';
    toast('已送出各馬達最低啟動 PWM');
    setTimeout(() => {
      if (mminPending > 0)
        $('mmin-state').textContent = '已送出，飛控確認 ' + (8 - mminPending) + '/8';
    }, 2000);
  } catch (e) {
    $('mmin-state').textContent = '下傳失敗: ' + e.message;
    toast('下傳失敗', 'err');
  } finally {
    btn.disabled = false;
  }
}
$('mmin-load').addEventListener('click', mminLoad);
$('mmin-save').addEventListener('click', mminSave);

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
      if (m.ser) { serLog(m.ser.dir === 'tx' ? 'tx' : 'rx', m.ser.data); return; }
      if (m.hello) return;
      serLog('rx', ev.data);
      if (m.gyro) applyTelemetry(m);
      else if (m.mmin) showMmin(m.mmin);
      else if (m.ack === 'motmin' && mminPending > 0) {
        mminPending--;
        if (mminPending === 0) $('mmin-state').textContent = '飛控已確認 8 顆設定';
      }
    } catch (e) {
      serLog('rx', ev.data);
    }
  };
  ws.onclose = () => { ws = null; setTimeout(() => { if (!$('app').classList.contains('hidden')) connectWs(); }, 3000); };
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
  let n;
  try { n = await api('/api/net'); } catch (e) { return; }
  document.querySelector('input[name=dhcp][value="' + (n.dhcp ? 1 : 0) + '"]').checked = true;
  $('net-ip').value = n.ip;
  $('net-gw').value = n.gw;
  $('net-mask').value = n.mask;
  toggleStatic();
}
function toggleStatic() {
  const dhcp = document.querySelector('input[name=dhcp]:checked').value === '1';
  $('static-fields').style.display = dhcp ? 'none' : 'block';
}
document.querySelectorAll('input[name=dhcp]').forEach(r => r.addEventListener('change', toggleStatic));
$('net-save').addEventListener('click', async () => {
  const dhcp = document.querySelector('input[name=dhcp]:checked').value;
  try {
    await api('/api/net', { method: 'POST', body: { dhcp, ip: $('net-ip').value, gw: $('net-gw').value, mask: $('net-mask').value } });
    toast('已儲存，裝置將重新啟動');
  } catch (e) { toast('儲存失敗', 'err'); }
});

/* ---------- RS232 串口監控 ---------- */
const SER_MAX = 400;               /* 顯示上限行數 */
let serPaused = false;
let serCount = { tx: 0, rx: 0 };

function serTs() {
  const d = new Date();
  const p = (n, w) => String(n).padStart(w || 2, '0');
  return p(d.getHours()) + ':' + p(d.getMinutes()) + ':' + p(d.getSeconds()) +
         '.' + p(d.getMilliseconds(), 3);
}

function serLog(dir, text) {
  serCount[dir]++;
  $('ser-tx-n').textContent = serCount.tx;
  $('ser-rx-n').textContent = serCount.rx;
  if (serPaused) return;

  const box = $('ser-log');
  const near = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
  const line = document.createElement('div');
  line.className = 'ser-line ' + dir;
  const ts = document.createElement('span');
  ts.className = 'ser-ts';
  ts.textContent = serTs();
  const badge = document.createElement('span');
  badge.className = 'ser-badge';
  badge.textContent = dir.toUpperCase();
  const body = document.createElement('span');
  body.className = 'ser-text';
  body.textContent = text;
  line.append(ts, badge, body);
  box.appendChild(line);

  while (box.children.length > SER_MAX) box.removeChild(box.firstChild);
  $('ser-show-n').textContent = box.children.length;
  if ($('ser-auto').checked && near) box.scrollTop = box.scrollHeight;
}

$('ser-pause').addEventListener('click', () => {
  serPaused = !serPaused;
  $('ser-pause').textContent = serPaused ? '繼續' : '暫停';
});
$('ser-clear').addEventListener('click', () => {
  $('ser-log').innerHTML = '';
  $('ser-show-n').textContent = '0';
});

function serSend() {
  const cmd = $('ser-input').value.trim();
  if (!cmd) return;
  if (cmd[0] !== '{') { toast('需為 JSON 字串，例如 {"cmd":"ping"}', 'err'); return; }
  api('/api/command', { method: 'POST', body: { cmd } })
    .then(() => { $('ser-input').value = ''; })   /* TX 由 WebSocket ser 帧回報顯示 */
    .catch((e) => toast('送出失敗: ' + e.message, 'err'));
}
$('ser-send').addEventListener('click', serSend);
$('ser-input').addEventListener('keydown', (e) => { if (e.key === 'Enter') serSend(); });

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
