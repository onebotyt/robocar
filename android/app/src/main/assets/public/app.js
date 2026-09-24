/**
 * NovaX Cyber Cockpit - 3-Card Mobile Car Controller
 * Full PWA & Native Capacitor Compatible
 */

// ================= STATE & CONFIGURATION =================
let base = localStorage.getItem('novaxBase') || 'http://192.168.4.1';
let isDrawMode = false;
let camOn = false;
let isConnected = false;
let sonarEnabled = true;
let currentMode = 'manual';

// DOM Helpers
const $ = (id) => document.getElementById(id);
const $$ = (sel) => document.querySelectorAll(sel);

// Haptic feedback
const vibrate = (ms = 15) => {
  try {
    if (navigator.vibrate) navigator.vibrate(ms);
  } catch (e) {}
};

// API Caller
const api = async (path, options = {}) => {
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), options.timeout || 3500);
  try {
    const res = await fetch(base + path, {
      cache: 'no-store',
      signal: controller.signal,
      ...options
    });
    clearTimeout(timeoutId);
    return res;
  } catch (err) {
    clearTimeout(timeoutId);
    throw err;
  }
};

const jsonApi = async (path, options = {}) => {
  const res = await api(path, options);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  return res.json();
};

// ================= TELEMETRY & STATUS =================
function setConnectionState(online) {
  isConnected = online;
  const dot = $('liveDot');
  if (dot) {
    if (online) dot.classList.add('online');
    else dot.classList.remove('online');
  }
  const statusEl = $('connStatusStat');
  if (statusEl) {
    if (online) {
      statusEl.textContent = 'Connected';
      statusEl.className = 'status-green';
    } else {
      statusEl.textContent = 'No connection';
      statusEl.className = 'status-red';
    }
  }
}

async function updateStatus() {
  try {
    const data = await jsonApi('/status');
    setConnectionState(true);

    // Distance in Radar
    if (data.distance !== undefined && data.distance !== null && $('radarDistVal')) {
      $('radarDistVal').textContent = data.distance;
    }

    // Battery / Ping estimate (if present in DOM)
    const bat = $('batteryStat');
    if (bat) bat.textContent = '98%';

    // Mode
    currentMode = data.mode ? 'manual' : 'auto';
    $('modeToggleBtn').textContent = currentMode.toUpperCase();
    $('modeToggleBtn').className = `hero-btn ${currentMode === 'manual' ? 'hero-blue' : 'hero-red'}`;

    // Wi-Fi Info in Settings
    if (data.wifiMode) $('wifiModeVal').textContent = data.wifiMode;
    if (data.ip) $('wifiIpVal').textContent = data.ip;
    if (data.version) {
      $('fwVersionVal').textContent = data.version;
      $('fwVerHeader').textContent = data.version;
    }
  } catch (err) {
    setConnectionState(false);
  }
}

// ================= DRIVE CONTROLS (D-PAD) =================
let moveInterval = null;
let currentMoveCmd = 'S';

async function sendMove(cmd) {
  currentMoveCmd = cmd;
  try {
    await api(`/move?d=${cmd}`);
  } catch (e) {}
}

$$('[data-move]').forEach((btn) => {
  const dir = btn.dataset.move;

  const startDrive = (e) => {
    e.preventDefault();
    vibrate(18);
    btn.classList.add('active');
    sendMove(dir);
    clearInterval(moveInterval);
    moveInterval = setInterval(() => sendMove(dir), 120);
  };

  const stopDrive = (e) => {
    e.preventDefault();
    btn.classList.remove('active');
    clearInterval(moveInterval);
    if (currentMoveCmd !== 'S') sendMove('S');
  };

  btn.addEventListener('pointerdown', startDrive);
  ['pointerup', 'pointercancel', 'pointerleave'].forEach((ev) =>
    btn.addEventListener(ev, stopDrive)
  );
});

// Stop Buttons
const stopCar = () => {
  vibrate(35);
  clearInterval(moveInterval);
  sendMove('S');
  api('/stop').catch(() => {});
};

$('stop').onclick = stopCar;
$('centerStopBtn').onclick = stopCar;

// Mode Toggle
$('modeToggleBtn').onclick = async () => {
  vibrate(20);
  const target = currentMode === 'manual' ? 'auto' : 'manual';
  await api(`/mode?set=${target}`).catch(() => {});
  updateStatus();
};

// Tactical Rotations
$('rotLBtn').onclick = () => { vibrate(20); api('/rotate?dir=left').catch(() => {}); };
$('rotRBtn').onclick = () => { vibrate(20); api('/rotate?dir=right').catch(() => {}); };
$('rot360Btn').onclick = () => { vibrate(25); api('/rotate?dir=360').catch(() => {}); };

// LED Effects
$$('[data-led]').forEach((btn) => {
  btn.onclick = async () => {
    vibrate(15);
    $$('[data-led]').forEach((b) => b.classList.remove('active'));
    btn.classList.add('active');
    const eff = btn.dataset.led;
    await api(`/led?effect=${eff}`).catch(() => {});
  };
});

// ================= DRAW MODE TOGGLE & CANVAS =================
const canvas = $('drawCanvas');
const ctx = canvas.getContext('2d');
let drawing = false;
let pathPoints = [];

function resizeCanvas() {
  const rect = canvas.parentElement.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  ctx.scale(dpr, dpr);
  redrawPath();
}

function getPos(e) {
  const rect = canvas.getBoundingClientRect();
  return {
    x: Math.min(1, Math.max(0, (e.clientX - rect.left) / rect.width)),
    y: Math.min(1, Math.max(0, (e.clientY - rect.top) / rect.height))
  };
}

function redrawPath() {
  const rect = canvas.getBoundingClientRect();
  ctx.clearRect(0, 0, rect.width, rect.height);
  if (pathPoints.length < 2) return;

  ctx.beginPath();
  ctx.strokeStyle = '#00bfa5';
  ctx.lineWidth = 4;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  ctx.shadowColor = '#00bfa5';
  ctx.shadowBlur = 8;

  ctx.moveTo(pathPoints[0].x * rect.width, pathPoints[0].y * rect.height);
  for (let i = 1; i < pathPoints.length; i++) {
    ctx.lineTo(pathPoints[i].x * rect.width, pathPoints[i].y * rect.height);
  }
  ctx.stroke();
  ctx.shadowBlur = 0;
}

canvas.onpointerdown = (e) => {
  drawing = true;
  canvas.setPointerCapture(e.pointerId);
  pathPoints = [getPos(e)];
  redrawPath();
};

canvas.onpointermove = (e) => {
  if (!drawing) return;
  pathPoints.push(getPos(e));
  redrawPath();
};

['pointerup', 'pointercancel'].forEach((ev) => {
  canvas.addEventListener(ev, () => {
    drawing = false;
  });
});

// Toggle between D-Pad and Canvas
$('drawModeBtn').onclick = () => {
  vibrate(20);
  isDrawMode = !isDrawMode;

  const dpadView = $('dpadView');
  const canvasView = $('canvasView');
  const modeTag = $('controlModeTag');
  const btn = $('drawModeBtn');

  if (isDrawMode) {
    dpadView.style.display = 'none';
    canvasView.style.display = 'block';
    modeTag.textContent = 'CANVAS';
    btn.textContent = 'Drive Mode';
    btn.classList.add('active');
    setTimeout(resizeCanvas, 50);
  } else {
    canvasView.style.display = 'none';
    dpadView.style.display = 'flex';
    modeTag.textContent = 'D-PAD';
    btn.textContent = 'Draw Mode';
    btn.classList.remove('active');
  }
};

$('clearBtn').onclick = () => {
  vibrate(15);
  pathPoints = [];
  const rect = canvas.getBoundingClientRect();
  ctx.clearRect(0, 0, rect.width, rect.height);
};

$('sendPathBtn').onclick = async () => {
  vibrate(30);
  if (pathPoints.length === 0) {
    alert('Draw a path on the canvas first!');
    return;
  }
  const body = pathPoints
    .map((p) => `${p.x.toFixed(3)},${p.y.toFixed(3)}`)
    .join(';');

  try {
    await api('/path', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body
    });
    alert('Path sent to NovaX!');
  } catch (err) {
    alert(`Send path failed: ${err.message}`);
  }
};

// ================= RADAR SWEEPER =================
$('scanBtn').onclick = async () => {
  vibrate(30);
  $('radarStatusTag').textContent = 'SCANNING';
  try {
    await api('/scan');
    setTimeout(async () => {
      try {
        const res = await jsonApi('/scanResult');
        $('radarStatusTag').textContent = 'DONE';

        const L = Number(res.left) || 400;
        const F = Number(res.front) || 400;
        const R = Number(res.right) || 400;

        $('radarDistVal').textContent = F;

        // Position blips
        const calcBottom = (dist) => `${Math.min(85, Math.max(15, (dist / 400) * 80))}%`;
        $('blipLeft').style.bottom = calcBottom(L);
        $('blipFront').style.bottom = calcBottom(F);
        $('blipRight').style.bottom = calcBottom(R);
      } catch (e) {
        $('radarStatusTag').textContent = 'TIMEOUT';
      }
    }, 1100);
  } catch (e) {
    $('radarStatusTag').textContent = 'FAILED';
  }
};

$('sonarToggleBtn').onclick = async () => {
  vibrate(20);
  sonarEnabled = !sonarEnabled;
  const btn = $('sonarToggleBtn');
  btn.textContent = sonarEnabled ? 'Sonar ON' : 'Sonar OFF';
  btn.classList.toggle('active', sonarEnabled);
  await api(`/sonarToggle?mode=${sonarEnabled ? 'on' : 'off'}`).catch(() => {});
};

// ================= LIVE CAMERA STREAM MODAL =================
let camTimer = null;
let camFrameCount = 0;
let lastFpsTime = performance.now();

if ($('openCamModalBtn') && $('cameraModal')) {
  $('openCamModalBtn').onclick = () => {
    $('cameraModal').style.display = 'flex';
  };
}

if ($('closeCamModal') && $('cameraModal')) {
  $('closeCamModal').onclick = () => {
    $('cameraModal').style.display = 'none';
  };
}

function fetchCamFrame() {
  if (!camOn) return;
  const img = $('cam');
  const tempImg = new Image();
  const frameUrl = `${base}/cam.jpg?t=${Date.now()}`;

  tempImg.onload = () => {
    img.src = frameUrl;
    camFrameCount++;
    const now = performance.now();
    if (now - lastFpsTime >= 1000) {
      $('camFpsVal').textContent = camFrameCount;
      camFrameCount = 0;
      lastFpsTime = now;
    }
    if (camOn) camTimer = setTimeout(fetchCamFrame, 350);
  };

  tempImg.onerror = () => {
    if (camOn) camTimer = setTimeout(fetchCamFrame, 1000);
  };

  tempImg.src = frameUrl;
}

$('camToggleBtn').onclick = () => {
  camOn = !camOn;
  vibrate(20);
  const btn = $('camToggleBtn');
  const standby = $('camStandby');
  const badge = $('camStateBadge');

  if (camOn) {
    btn.textContent = 'Camera OFF';
    badge.textContent = 'LIVE';
    badge.style.color = '#10b981';
    standby.classList.remove('active');
    fetchCamFrame();
  } else {
    btn.textContent = 'Camera ON';
    badge.textContent = 'OFF';
    badge.style.color = 'var(--text-dim)';
    standby.classList.add('active');
    clearTimeout(camTimer);
    $('camFpsVal').textContent = '0';
  }
};

$('camSnapBtn').onclick = () => {
  vibrate(25);
  const img = $('cam');
  if (!img.src || !camOn) {
    alert('Turn Camera ON first to take a snapshot.');
    return;
  }
  const a = document.createElement('a');
  a.href = img.src;
  a.download = `novax_${Date.now()}.jpg`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
};

// ================= SETTINGS & OTA MODAL =================
$('openSettingsModalBtn').onclick = () => {
  $('settingsModal').style.display = 'flex';
};

$('closeSettingsModal').onclick = () => {
  $('settingsModal').style.display = 'none';
};

function applyHost(newHost) {
  if (!newHost) return;
  newHost = newHost.trim().replace(/\/+$/, '');
  if (!newHost.startsWith('http://') && !newHost.startsWith('https://')) {
    newHost = 'http://' + newHost;
  }
  base = newHost;
  localStorage.setItem('novaxBase', base);
  $('hostUrlInput').value = base;
  updateStatus();
}

$('saveHostBtn').onclick = () => {
  vibrate(15);
  applyHost($('hostUrlInput').value);
  alert(`Connected to ${base}`);
};

$$('.quick-host-pill').forEach((pill) => {
  pill.onclick = () => {
    vibrate(10);
    applyHost(pill.dataset.ip);
  };
});

// Fullscreen
$('fullscreenBtn').onclick = () => {
  vibrate(15);
  if (!document.fullscreenElement) {
    document.documentElement.requestFullscreen().catch(() => {});
  } else {
    document.exitFullscreen().catch(() => {});
  }
};

// Wi-Fi Management
$('scanWifiBtn').onclick = async () => {
  vibrate(25);
  const el = $('wifiNetworksList');
  el.innerHTML = '<div style="color:var(--color-teal)">Scanning Wi-Fi...</div>';
  try {
    const data = await jsonApi('/wifi/scan');
    el.innerHTML = '';
    if (!data.networks || data.networks.length === 0) {
      el.innerHTML = '<div>No networks found.</div>';
      return;
    }
    data.networks.forEach((n) => {
      const item = document.createElement('div');
      item.innerHTML = `
        <span>📶 <b>${n.ssid}</b> (${n.rssi} dBm)</span>
        <button class="pill-btn blue" style="padding:2px 8px;font-size:0.68rem">Save</button>
      `;
      item.querySelector('button').onclick = () => {
        const pass = prompt(`Password for "${n.ssid}":`, '');
        if (pass !== null) {
          api(`/wifi/save?ssid=${encodeURIComponent(n.ssid)}&password=${encodeURIComponent(pass)}`)
            .then(loadSavedWifi);
        }
      };
      el.appendChild(item);
    });
  } catch (e) {
    el.innerHTML = '<div style="color:var(--color-red)">Wi-Fi scan failed.</div>';
  }
};

async function loadSavedWifi() {
  try {
    const data = await jsonApi('/wifi/saved');
    const el = $('savedWifiList');
    el.innerHTML = '';
    if (!data.networks || data.networks.length === 0) {
      el.innerHTML = '<div>No networks saved.</div>';
      return;
    }
    data.networks.forEach((n, i) => {
      const isSel = i === data.selected;
      const item = document.createElement('div');
      item.innerHTML = `
        <span>${isSel ? '⭐ ' : ''}<b>${n.ssid}</b></span>
        <span>
          <button class="pill-btn" style="padding:2px 6px;font-size:0.65rem">${isSel ? 'USE' : 'Use'}</button>
          <button class="pill-btn red" style="padding:2px 6px;font-size:0.65rem">Del</button>
        </span>
      `;
      item.querySelectorAll('button')[0].onclick = () => api(`/wifi/select?index=${i}`).then(loadSavedWifi);
      item.querySelectorAll('button')[1].onclick = () => {
        if (confirm(`Delete ${n.ssid}?`)) api(`/wifi/delete?index=${i}`).then(loadSavedWifi);
      };
      el.appendChild(item);
    });
  } catch (e) {}
}

$('switchStaBtn').onclick = async () => {
  vibrate(25);
  try {
    const res = await jsonApi('/wifi/switchSta');
    applyHost(res.host);
    alert(`Switched to STA mode. Connect phone to ${res.ssid} then use ${res.host}`);
  } catch (e) {
    alert('STA switch failed.');
  }
};

$('switchApBtn').onclick = async () => {
  vibrate(25);
  try {
    await api('/wifi/switchAp');
    applyHost('http://192.168.4.1');
    alert('Switched to AP mode. Connect to NovaX-V2.');
  } catch (e) {}
};

// Firmware OTA
$('fwUpdateBtn').onclick = async () => {
  vibrate(35);
  const file = $('fwFileInput').files[0];
  if (!file) {
    alert('Select an ESP32 .bin file first.');
    return;
  }
  const token = $('otaTokenInput').value.trim() || 'NovaX-OTA-ChangeMe';
  if (!confirm(`Flash "${file.name}" to ESP32? Robot will stop and restart.`)) return;

  const msg = $('fwMsg');
  msg.textContent = 'Uploading firmware...';
  try {
    const res = await api('/ota/update', {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream', 'X-NovaX-OTA': token },
      body: file,
      timeout: 60000
    });
    if (!res.ok) throw new Error(await res.text());
    msg.textContent = 'Upload successful! ESP32 restarting...';
  } catch (err) {
    msg.textContent = `OTA Error: ${err.message}`;
  }
};

// ================= GITHUB APP UPDATER SYSTEM =================
const CURRENT_APP_VERSION = '2.2.0';

function getGitHubConfig() {
  const repo = $('ghRepoInput')?.value.trim() || localStorage.getItem('novax_gh_repo') || 'onebotyt/robocar';
  return { repo, branch: 'main' };
}

function setUpdateLog(msg, append = true) {
  const box = $('updateLogBox');
  if (!box) return;
  box.style.display = 'block';
  box.textContent = append && box.textContent ? box.textContent + '\n' + msg : msg;
  box.scrollTop = box.scrollHeight;
}

async function checkGithubUpdates(interactive = true) {
  const { repo, branch } = getGitHubConfig();
  localStorage.setItem('novax_gh_repo', repo);

  const activeVer = localStorage.getItem('novax_hot_version') || CURRENT_APP_VERSION;
  setUpdateLog(`Checking GitHub (${repo}@${branch})...`, false);

  try {
    const res = await fetch(`https://raw.githubusercontent.com/${repo}/${branch}/pwa/version.json?t=${Date.now()}`, { cache: 'no-store' });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const data = await res.json();
    const remoteVer = (data.version || '').trim();
    $('remoteAppVer').textContent = `v${remoteVer}`;

    if (remoteVer && remoteVer !== activeVer) {
      setUpdateLog(`New version: v${remoteVer} (Current: v${activeVer})`);
      $('appUpdateBanner').style.display = 'flex';
      $('updateBannerTitle').textContent = `NovaX v${remoteVer} Ready`;
      if (data.changelog) $('updateBannerDesc').textContent = data.changelog;
      return data;
    } else {
      setUpdateLog(`Up to date (v${activeVer})`);
      if (interactive) alert(`NovaX is up to date (v${activeVer})!`);
      return null;
    }
  } catch (err) {
    setUpdateLog(`Update check failed: ${err.message}`);
    if (interactive) alert(`Update check error: ${err.message}`);
    return null;
  }
}

async function hotUpdateApp() {
  vibrate(30);
  const { repo, branch } = getGitHubConfig();
  const baseUrl = `https://raw.githubusercontent.com/${repo}/${branch}/pwa`;
  setUpdateLog('Starting Hot Update from GitHub...', false);

  try {
    setUpdateLog('1/3 Fetching version.json...');
    const verRes = await fetch(`${baseUrl}/version.json?t=${Date.now()}`, { cache: 'no-store' });
    const verData = await verRes.json();
    const newVer = verData.version || '2.2.0';

    setUpdateLog('2/3 Downloading style.css...');
    const cssRes = await fetch(`${baseUrl}/style.css?t=${Date.now()}`, { cache: 'no-store' });
    const cssText = await cssRes.text();

    setUpdateLog('3/3 Downloading app.js...');
    const jsRes = await fetch(`${baseUrl}/app.js?t=${Date.now()}`, { cache: 'no-store' });
    const jsText = await jsRes.text();

    localStorage.setItem('novax_hot_version', newVer);
    localStorage.setItem('novax_hot_css', cssText);
    localStorage.setItem('novax_hot_js', jsText);

    setUpdateLog(`Installed! Reloading NovaX v${newVer}...`);
    vibrate(40);
    setTimeout(() => window.location.reload(), 1200);
  } catch (err) {
    setUpdateLog(`Update failed: ${err.message}`);
    alert(`Update Error: ${err.message}`);
  }
}

function factoryResetApp() {
  vibrate(25);
  if (!confirm('Revert back to the factory bundled APK code?')) return;
  localStorage.removeItem('novax_hot_version');
  localStorage.removeItem('novax_hot_css');
  localStorage.removeItem('novax_hot_js');
  alert('Reset to bundle. Reloading...');
  window.location.reload();
}

$('checkGhUpdateBtn').onclick = () => checkGithubUpdates(true);
$('hotUpdateBtn').onclick = hotUpdateApp;
$('factoryResetAppBtn').onclick = factoryResetApp;
$('downloadApkBtn').onclick = () => {
  const { repo } = getGitHubConfig();
  window.open(`https://github.com/${repo}/releases/latest`, '_blank');
};

$('bannerUpdateBtn').onclick = () => {
  $('appUpdateBanner').style.display = 'none';
  hotUpdateApp();
};
$('bannerDismissBtn').onclick = () => {
  $('appUpdateBanner').style.display = 'none';
};

// ================= INITIALIZATION =================
window.addEventListener('DOMContentLoaded', () => {
  $('hostUrlInput').value = base;

  const savedRepo = localStorage.getItem('novax_gh_repo') || 'onebotyt/robocar';
  if ($('ghRepoInput')) $('ghRepoInput').value = savedRepo;

  const activeVer = localStorage.getItem('novax_hot_version') || CURRENT_APP_VERSION;
  $('installedAppVer').textContent = `v${activeVer}`;

  loadSavedWifi();
  updateStatus();
  setInterval(updateStatus, 1200);

  window.addEventListener('resize', () => {
    if (isDrawMode) resizeCanvas();
  });

  // Check GitHub updates on startup if online
  if (navigator.onLine && savedRepo) {
    setTimeout(() => checkGithubUpdates(false), 2500);
  }

  // Register service worker for offline app loading
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./sw.js').catch(() => {});
  }
});
