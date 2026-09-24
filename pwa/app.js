/**
 * NovaX Cyber Cockpit - High Performance Mobile Car Controller
 * Full PWA & Native Capacitor Compatible
 */

// ================= STATE & CONFIGURATION =================
let base = localStorage.getItem('novaxBase') || 'http://192.168.4.1';
let camOn = false;
let isConnected = false;
let lastPingTime = 0;
let deferredInstallPrompt = null;
let currentTab = 'view-drive';

// LED simulation loop timer
let ledSimInterval = null;
let currentLedEffect = 'off';

// DOM Helper
const $ = (id) => document.getElementById(id);
const $$ = (sel) => document.querySelectorAll(sel);

// Haptic feedback
const vibrate = (ms = 15) => {
  try {
    if (navigator.vibrate) navigator.vibrate(ms);
  } catch (e) {}
};

// Robust API caller with timeout & CORS support
const api = async (path, options = {}) => {
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), options.timeout || 4000);
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

// ================= CONNECTION STATUS & TELEMETRY =================
function setConnectionState(status, ping = null) {
  const pill = $('hostPillBtn');
  const statusEl = $('connStatus');
  const pingEl = $('pingTag');

  pill.classList.remove('connected', 'connecting');

  if (status === 'connected') {
    pill.classList.add('connected');
    statusEl.textContent = 'ONLINE';
    if (ping !== null) pingEl.textContent = `${ping}ms`;
    isConnected = true;
  } else if (status === 'connecting') {
    pill.classList.add('connecting');
    statusEl.textContent = 'CONNECTING';
    isConnected = false;
  } else {
    statusEl.textContent = 'OFFLINE';
    pingEl.textContent = '--ms';
    isConnected = false;
  }
}

async function updateStatus() {
  const start = performance.now();
  try {
    const data = await jsonApi('/status');
    const ping = Math.round(performance.now() - start);
    setConnectionState('connected', ping);

    // Update Proximity Gauge
    const dist = Number(data.distance);
    const distEl = $('distance');
    const proxCard = $('proximityCard');
    const proxBar = $('proximityBar');
    const proxStatus = $('proximityStatus');

    if (!isNaN(dist) && dist >= 0) {
      distEl.textContent = dist;
      const barPct = Math.min(100, Math.max(5, (dist / 120) * 100));
      proxBar.style.width = `${barPct}%`;

      proxCard.classList.remove('warning', 'danger');
      if (dist < 25) {
        proxCard.classList.add('danger');
        proxStatus.textContent = 'OBSTACLE HAZARD';
      } else if (dist < 60) {
        proxCard.classList.add('warning');
        proxStatus.textContent = 'APPROACHING';
      } else {
        proxStatus.textContent = 'CLEAR PATH';
      }
    } else {
      distEl.textContent = '--';
      proxStatus.textContent = 'STANDBY';
    }

    // Update Gyro Heading
    const yaw = Number(data.yaw) || 0;
    $('yaw').textContent = yaw.toFixed(1);
    $('yawLarge').textContent = `${yaw.toFixed(1)}°`;

    // Rotate compass dials
    const compassDial = $('compassDial');
    if (compassDial) compassDial.style.transform = `rotate(${yaw}deg)`;
    const largeCompassNeedle = document.querySelector('.dial-needle');
    if (largeCompassNeedle) largeCompassNeedle.style.transform = `rotate(${yaw}deg)`;

    // Update Mode
    const isManual = Boolean(data.mode);
    const modeText = isManual ? 'MANUAL' : 'AUTO';
    $('robotModeText').textContent = modeText;

    // Update Wi-Fi Info
    if (data.wifiMode) $('wifiMode').textContent = data.wifiMode;
    if (data.ip) $('wifiIp').textContent = data.ip;
    if (data.version) {
      $('fwVersionBadge').textContent = data.version;
      $('fwVersion').textContent = data.version;
    }
  } catch (err) {
    setConnectionState('offline');
  }
}

// ================= DRIVE CONTROLS (D-PAD & JOYSTICK) =================
let moveInterval = null;
let currentMoveCmd = 'S';

async function sendMove(cmd) {
  currentMoveCmd = cmd;
  try {
    await api(`/move?d=${cmd}`);
  } catch (e) {
    // offline or network dropped
  }
}

// D-Pad pointer setup (Multi-touch & haptic)
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

// Emergency Stop
const triggerEmergencyStop = () => {
  vibrate(40);
  clearInterval(moveInterval);
  sendMove('S');
  api('/stop').catch(() => {});
};

$('stop').addEventListener('click', triggerEmergencyStop);
$('emergencyStop').addEventListener('click', triggerEmergencyStop);

// Tactical Rotations & Modes
$('rotL').onclick = () => { vibrate(20); api('/rotate?dir=left'); };
$('rotR').onclick = () => { vibrate(20); api('/rotate?dir=right'); };
$('rot360').onclick = () => { vibrate(25); api('/rotate?dir=360'); };

$('modeQuickBtn').onclick = async () => {
  vibrate(20);
  const current = $('robotModeText').textContent;
  const target = current === 'MANUAL' ? 'auto' : 'manual';
  await api(`/mode?set=${target}`);
  updateStatus();
};

// Control Tabs Switcher (D-Pad vs Joystick)
$('tabDpad').onclick = () => {
  $('tabDpad').classList.add('active');
  $('tabJoy').classList.remove('active');
  $('dpadContainer').style.display = 'flex';
  $('joyContainer').style.display = 'none';
};

$('tabJoy').onclick = () => {
  $('tabJoy').classList.add('active');
  $('tabDpad').classList.remove('active');
  $('joyContainer').style.display = 'flex';
  $('dpadContainer').style.display = 'none';
};

// Virtual Analog Joystick Logic
const joyZone = $('joyZone');
const joyStick = $('joyStick');
let joyActive = false;
let joyCenter = { x: 0, y: 0 };
let joyLastCmd = 'S';
let joyThrottleTimer = null;

const maxRadius = 60; // Stick travel limit

joyZone.addEventListener('pointerdown', (e) => {
  joyActive = true;
  joyZone.setPointerCapture(e.pointerId);
  const rect = joyZone.getBoundingClientRect();
  joyCenter = { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
  handleJoyMove(e);
});

joyZone.addEventListener('pointermove', (e) => {
  if (!joyActive) return;
  handleJoyMove(e);
});

const resetJoy = () => {
  joyActive = false;
  joyStick.style.transform = `translate(0px, 0px)`;
  $('joyAngle').textContent = '0°';
  $('joyDir').textContent = 'IDLE';
  $('joyPower').textContent = '0%';
  if (joyLastCmd !== 'S') {
    joyLastCmd = 'S';
    sendMove('S');
  }
};

['pointerup', 'pointercancel', 'pointerleave'].forEach((ev) =>
  joyZone.addEventListener(ev, resetJoy)
);

function handleJoyMove(e) {
  const dx = e.clientX - joyCenter.x;
  const dy = e.clientY - joyCenter.y;
  const dist = Math.hypot(dx, dy);
  const angleRad = Math.atan2(dy, dx);
  let angleDeg = Math.round((angleRad * 180) / Math.PI);
  if (angleDeg < 0) angleDeg += 360;

  const clampedDist = Math.min(dist, maxRadius);
  const stickX = Math.cos(angleRad) * clampedDist;
  const stickY = Math.sin(angleRad) * clampedDist;

  joyStick.style.transform = `translate(${stickX}px, ${stickY}px)`;

  const power = Math.round((clampedDist / maxRadius) * 100);
  $('joyAngle').textContent = `${angleDeg}°`;
  $('joyPower').textContent = `${power}%`;

  let nextCmd = 'S';
  let dirText = 'IDLE';

  if (power > 25) {
    // 8-quadrant direction mapping
    if (angleDeg >= 45 && angleDeg < 135) {
      nextCmd = 'B';
      dirText = 'REV';
    } else if (angleDeg >= 135 && angleDeg < 225) {
      nextCmd = 'L';
      dirText = 'LEFT';
    } else if (angleDeg >= 225 && angleDeg < 315) {
      nextCmd = 'F';
      dirText = 'FWD';
    } else {
      nextCmd = 'R';
      dirText = 'RIGHT';
    }
  }

  $('joyDir').textContent = dirText;

  if (nextCmd !== joyLastCmd) {
    joyLastCmd = nextCmd;
    vibrate(10);
    sendMove(nextCmd);
  }
}

// ================= LIVE CAMERA STREAM =================
let camFrameCount = 0;
let lastFpsTime = performance.now();
let camTimer = null;

function updateCameraFps() {
  const now = performance.now();
  if (now - lastFpsTime >= 1000) {
    $('camFps').textContent = camFrameCount;
    camFrameCount = 0;
    lastFpsTime = now;
  }
}

function fetchNextFrame() {
  if (!camOn) return;
  const img = $('cam');
  const tempImg = new Image();
  const frameUrl = `${base}/cam.jpg?t=${Date.now()}`;

  tempImg.onload = () => {
    img.src = frameUrl;
    camFrameCount++;
    updateCameraFps();
    $('recDot').style.display = 'inline-block';
    $('camStateText').textContent = 'LIVE';
    if (camOn) camTimer = setTimeout(fetchNextFrame, 350);
  };

  tempImg.onerror = () => {
    $('recDot').style.display = 'none';
    $('camStateText').textContent = 'STANDBY';
    if (camOn) camTimer = setTimeout(fetchNextFrame, 1000);
  };

  tempImg.src = frameUrl;
}

$('camToggle').onclick = () => {
  camOn = !camOn;
  vibrate(20);
  const standby = $('camStandby');
  const toggleText = $('camToggleText');

  if (camOn) {
    toggleText.textContent = 'CAMERA OFF';
    standby.classList.remove('active');
    fetchNextFrame();
  } else {
    toggleText.textContent = 'CAMERA ON';
    standby.classList.add('active');
    clearTimeout(camTimer);
    $('camFps').textContent = '0';
    $('recDot').style.display = 'none';
    $('camStateText').textContent = 'STANDBY';
  }
};

$('snapshotBtn').onclick = () => {
  vibrate(25);
  const img = $('cam');
  if (!img.src || !camOn) {
    alert('Please turn Camera ON first to take a snapshot.');
    return;
  }
  const a = document.createElement('a');
  a.href = img.src;
  a.download = `novax_capture_${Date.now()}.jpg`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
};

// ================= 180° RADAR SWEEPER =================
$('scan').onclick = async () => {
  vibrate(30);
  $('radarStatusBadge').textContent = 'SWEEPING...';
  try {
    await api('/scan');
    // Poll scanResult after servo sweep completes (~900ms)
    setTimeout(async () => {
      try {
        const res = await jsonApi('/scanResult');
        $('radarStatusBadge').textContent = 'SCAN COMPLETE';

        const L = Number(res.left) || 400;
        const F = Number(res.front) || 400;
        const R = Number(res.right) || 400;

        $('leftVal').textContent = `${L} cm`;
        $('frontVal').textContent = `${F} cm`;
        $('rightVal').textContent = `${R} cm`;

        $('blipLeftText').textContent = `L: ${L}cm`;
        $('blipFrontText').textContent = `F: ${F}cm`;
        $('blipRightText').textContent = `R: ${R}cm`;

        // Position blips dynamically on radar arc
        const calcBottom = (dist) => `${Math.min(90, Math.max(15, (dist / 400) * 85))}%`;
        $('blipLeft').style.bottom = calcBottom(L);
        $('blipFront').style.bottom = calcBottom(F);
        $('blipRight').style.bottom = calcBottom(R);
      } catch (e) {
        $('radarStatusBadge').textContent = 'SCAN TIMEOUT';
      }
    }, 1100);
  } catch (e) {
    $('radarStatusBadge').textContent = 'SCAN FAILED';
  }
};

let sonarEnabled = true;
$('sonarToggleBtn').onclick = async () => {
  vibrate(20);
  sonarEnabled = !sonarEnabled;
  $('sonarToggleText').textContent = `SONAR: ${sonarEnabled ? 'ON' : 'OFF'}`;
  await api(`/sonarToggle?mode=${sonarEnabled ? 'on' : 'off'}`);
};

// ================= AUTONAV PATH DRAW (CANVAS) =================
const canvas = $('draw');
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
  ctx.strokeStyle = '#00f2fe';
  ctx.lineWidth = 4;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  ctx.shadowColor = '#00f2fe';
  ctx.shadowBlur = 10;

  ctx.moveTo(pathPoints[0].x * rect.width, pathPoints[0].y * rect.height);
  for (let i = 1; i < pathPoints.length; i++) {
    ctx.lineTo(pathPoints[i].x * rect.width, pathPoints[i].y * rect.height);
  }
  ctx.stroke();
  ctx.shadowBlur = 0;
}

function updateTrajectoryPrediction() {
  $('pathPointsBadge').textContent = `${pathPoints.length} POINTS`;
  if (pathPoints.length < 2) {
    $('trajPrediction').textContent = 'DRAW A PATH';
    return;
  }
  const avgX = pathPoints.reduce((acc, p) => acc + p.x, 0) / pathPoints.length;
  if (avgX < 0.45) {
    $('trajPrediction').textContent = 'LEFT STEER (TURBO RIGHT)';
  } else if (avgX > 0.55) {
    $('trajPrediction').textContent = 'RIGHT STEER (TURBO LEFT)';
  } else {
    $('trajPrediction').textContent = 'STRAIGHT CRUISE';
  }
}

canvas.onpointerdown = (e) => {
  drawing = true;
  canvas.setPointerCapture(e.pointerId);
  pathPoints = [getPos(e)];
  updateTrajectoryPrediction();
  redrawPath();
};

canvas.onpointermove = (e) => {
  if (!drawing) return;
  const pt = getPos(e);
  pathPoints.push(pt);
  updateTrajectoryPrediction();
  redrawPath();
};

['pointerup', 'pointercancel'].forEach((ev) => {
  canvas.addEventListener(ev, () => {
    drawing = false;
  });
});

$('clear').onclick = () => {
  vibrate(15);
  pathPoints = [];
  updateTrajectoryPrediction();
  const rect = canvas.getBoundingClientRect();
  ctx.clearRect(0, 0, rect.width, rect.height);
};

$('sendPath').onclick = async () => {
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
    alert('Path transmitted to NovaX! Autonav engaged.');
  } catch (err) {
    alert(`Failed to send path: ${err.message}`);
  }
};

// ================= 74HC595 LED CONTROLLER =================
function renderLeds(byteVal, isWarn = false) {
  const leds = $$('.cyber-led');
  leds.forEach((led, idx) => {
    const isLit = Boolean((byteVal >> (7 - idx)) & 1);
    led.classList.remove('lit', 'warn-lit');
    if (isLit) {
      led.classList.add(isWarn ? 'warn-lit' : 'lit');
    }
  });
}

function startLedSimulation(effect) {
  clearInterval(ledSimInterval);
  currentLedEffect = effect;
  $('ledEffectBadge').textContent = effect.toUpperCase();

  $$('.led-card-btn').forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.led === effect);
  });

  if (effect === 'off') {
    renderLeds(0x00);
  } else if (effect === 'blink') {
    let on = false;
    ledSimInterval = setInterval(() => {
      on = !on;
      renderLeds(on ? 0xff : 0x00);
    }, 300);
  } else if (effect === 'warn') {
    let on = false;
    ledSimInterval = setInterval(() => {
      on = !on;
      renderLeds(on ? 0xc3 : 0x00, true);
    }, 500);
  } else if (effect === 'pulse') {
    let p = 0x18;
    ledSimInterval = setInterval(() => {
      p = p === 0x18 ? 0x3c : 0x18;
      renderLeds(p);
    }, 250);
  }
}

$$('[data-led]').forEach((btn) => {
  btn.onclick = async () => {
    vibrate(20);
    const eff = btn.dataset.led;
    startLedSimulation(eff);
    await api(`/led?effect=${eff}`).catch(() => {});
  };
});

// ================= WI-FI & SAVED NETWORKS =================
$('wifiScan').onclick = async () => {
  vibrate(25);
  const el = $('networks');
  el.innerHTML = '<div style="color:var(--neon-cyan)">Scanning nearby Wi-Fi networks...</div>';
  try {
    const data = await jsonApi('/wifi/scan');
    el.innerHTML = '';
    if (!data.networks || data.networks.length === 0) {
      el.innerHTML = '<div>No networks discovered.</div>';
      return;
    }
    data.networks.forEach((n) => {
      const item = document.createElement('div');
      item.innerHTML = `
        <span>📶 <b>${n.ssid}</b> <small style="color:var(--text-dim)">(${n.rssi} dBm)</small></span>
        <button class="save-net-btn">Connect</button>
      `;
      item.querySelector('.save-net-btn').onclick = () => {
        const pass = prompt(`Enter password for "${n.ssid}":`, '');
        if (pass !== null) {
          api(
            `/wifi/save?ssid=${encodeURIComponent(n.ssid)}&password=${encodeURIComponent(pass)}`
          ).then(loadSavedWifi);
        }
      };
      el.appendChild(item);
    });
  } catch (e) {
    el.innerHTML = '<div style="color:var(--neon-red)">Wi-Fi scan failed. Check connection.</div>';
  }
};

async function loadSavedWifi() {
  try {
    const data = await jsonApi('/wifi/saved');
    const el = $('saved');
    el.innerHTML = '';
    if (!data.networks || data.networks.length === 0) {
      el.innerHTML = '<div style="color:var(--text-dim)">No networks saved in ESP32 NVS.</div>';
      return;
    }
    data.networks.forEach((n, i) => {
      const isSel = i === data.selected;
      const item = document.createElement('div');
      item.innerHTML = `
        <span>${isSel ? '⭐ ' : ''}<b>${n.ssid}</b></span>
        <span>
          <button class="use-btn">${isSel ? 'ACTIVE' : 'USE'}</button>
          <button class="del-btn" style="color:var(--neon-red)">DEL</button>
        </span>
      `;
      item.querySelector('.use-btn').onclick = () => {
        vibrate(15);
        api(`/wifi/select?index=${i}`).then(loadSavedWifi);
      };
      item.querySelector('.del-btn').onclick = () => {
        if (confirm(`Delete saved network "${n.ssid}"?`)) {
          api(`/wifi/delete?index=${i}`).then(loadSavedWifi);
        }
      };
      el.appendChild(item);
    });
  } catch (e) {}
}

$('switchSta').onclick = async () => {
  vibrate(30);
  try {
    const res = await jsonApi('/wifi/switchSta');
    localStorage.setItem('novaxBase', res.host);
    base = res.host;
    $('hostUrlInput').value = res.host;
    alert(`Switching to STA mode. Connect your phone to "${res.ssid}" then access NovaX at ${res.host}`);
    setTimeout(updateStatus, 5000);
  } catch (e) {
    alert('STA switch failed. Ensure a network is selected in Saved Profiles.');
  }
};

$('switchAp').onclick = async () => {
  vibrate(30);
  try {
    await api('/wifi/switchAp');
    localStorage.setItem('novaxBase', 'http://192.168.4.1');
    base = 'http://192.168.4.1';
    $('hostUrlInput').value = base;
    alert('NovaX switching to AP mode. Connect phone to "NovaX-V2" Wi-Fi.');
    setTimeout(updateStatus, 3000);
  } catch (e) {}
};

// ================= FIRMWARE OTA UPDATER =================
async function loadOtaStatus() {
  try {
    const res = await jsonApi('/ota/status');
    if (res.version) $('fwVersion').textContent = res.version;
  } catch (e) {}
}

$('fwUpdate').onclick = async () => {
  vibrate(40);
  const fileInput = $('fwFile');
  const file = fileInput.files[0];
  if (!file) {
    alert('Please select an ESP32 firmware .bin file first.');
    return;
  }

  const token = $('otaTokenInput').value.trim() || 'NovaX-OTA-ChangeMe';
  if (!confirm(`Warning: NovaX robot will stop and flash "${file.name}". Proceed?`)) return;

  const msgEl = $('fwMsg');
  msgEl.style.color = 'var(--neon-cyan)';
  msgEl.textContent = 'Uploading firmware binary to ESP32...';

  try {
    const res = await api('/ota/update', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/octet-stream',
        'X-NovaX-OTA': token
      },
      body: file,
      timeout: 60000
    });
    const respText = await res.text();
    if (!res.ok) throw new Error(respText || `Status ${res.status}`);
    msgEl.style.color = 'var(--neon-green)';
    msgEl.textContent = 'Upload successful! ESP32 is rebooting. Reconnecting in 8s...';
    setTimeout(updateStatus, 8000);
  } catch (err) {
    msgEl.style.color = 'var(--neon-red)';
    msgEl.textContent = `OTA Flash Error: ${err.message}`;
  }
};

// ================= HOST MODAL & CONFIG =================
function applyHost(newHost) {
  if (!newHost) return;
  newHost = newHost.trim().replace(/\/+$/, '');
  if (!newHost.startsWith('http://') && !newHost.startsWith('https://')) {
    newHost = 'http://' + newHost;
  }
  base = newHost;
  localStorage.setItem('novaxBase', base);
  $('hostUrlInput').value = base;
  $('modalHostInput').value = base;
  setConnectionState('connecting');
  updateStatus();
}

$('saveHostBtn').onclick = () => {
  vibrate(15);
  applyHost($('hostUrlInput').value);
};

$('hostPillBtn').onclick = () => {
  $('modalHostInput').value = base;
  $('hostModal').style.display = 'flex';
};

$('closeHostModal').onclick = () => {
  $('hostModal').style.display = 'none';
};

$('modalSaveHost').onclick = () => {
  vibrate(15);
  applyHost($('modalHostInput').value);
  $('hostModal').style.display = 'none';
};

$$('.preset-pill, .modal-pill').forEach((pill) => {
  pill.onclick = () => {
    vibrate(10);
    applyHost(pill.dataset.ip);
    $('hostModal').style.display = 'none';
  };
});

// Fullscreen immersion
$('btnFullscreen').onclick = () => {
  vibrate(15);
  if (!document.fullscreenElement) {
    document.documentElement.requestFullscreen().catch(() => {});
  } else {
    document.exitFullscreen().catch(() => {});
  }
};

// ================= PWA INSTALLATION =================
window.addEventListener('beforeinstallprompt', (e) => {
  e.preventDefault();
  deferredInstallPrompt = e;
  const pwaBtn = $('pwaInstallBtn');
  if (pwaBtn) pwaBtn.style.display = 'block';
});

$('pwaInstallBtn').onclick = async () => {
  vibrate(20);
  if (deferredInstallPrompt) {
    deferredInstallPrompt.prompt();
    const { outcome } = await deferredInstallPrompt.userChoice;
    if (outcome === 'accepted') {
      $('pwaInstallBtn').style.display = 'none';
    }
    deferredInstallPrompt = null;
  } else {
    alert('To install NovaX on your phone:\n- On Android: Tap browser menu (⋮) -> "Add to Home screen" or "Install App".\n- On iOS: Tap Share button -> "Add to Home Screen".');
  }
};

// ================= BOTTOM TAB NAVIGATION =================
$$('.nav-item').forEach((item) => {
  item.onclick = () => {
    vibrate(15);
    const targetView = item.dataset.view;
    if (targetView === currentTab) return;

    $$('.nav-item').forEach((btn) => btn.classList.remove('active'));
    item.classList.add('active');

    $$('.view-panel').forEach((panel) => panel.classList.remove('active'));
    $(targetView).classList.add('active');
    currentTab = targetView;

    if (targetView === 'view-path') {
      setTimeout(resizeCanvas, 50);
    }
  };
});

window.addEventListener('resize', () => {
  if (currentTab === 'view-path') resizeCanvas();
});

// ================= GITHUB APP UPDATER SYSTEM =================
const CURRENT_APP_VERSION = '2.2.0';

function getGitHubConfig() {
  const repoInput = $('ghRepoInput');
  const branchInput = $('ghBranchInput');
  const repo = repoInput ? repoInput.value.trim() : localStorage.getItem('novax_gh_repo') || 'onebotyt/robocar';
  const branch = branchInput ? branchInput.value.trim() || 'main' : localStorage.getItem('novax_gh_branch') || 'main';
  return { repo, branch };
}

function setUpdateLog(msg, append = true) {
  const box = $('updateLog');
  if (!box) return;
  box.style.display = 'block';
  if (append && box.textContent) {
    box.textContent += '\n' + msg;
  } else {
    box.textContent = msg;
  }
  box.scrollTop = box.scrollHeight;
}

function showUpdateBanner(ver, changelog) {
  const banner = $('appUpdateBanner');
  if (!banner) return;
  $('updateBannerTitle').textContent = `NovaX v${ver} Ready`;
  if (changelog) $('updateBannerDesc').textContent = changelog;
  banner.style.display = 'flex';
}

function hideUpdateBanner() {
  const banner = $('appUpdateBanner');
  if (banner) banner.style.display = 'none';
}

async function checkGithubUpdates(interactive = true) {
  const { repo, branch } = getGitHubConfig();

  if (!repo) {
    if (interactive) {
      alert('Please enter your GitHub repository first (e.g. username/NovaX_PWA_OTA_V2).');
      $('ghRepoInput')?.focus();
    }
    return null;
  }

  // Persist repo & branch in localStorage
  localStorage.setItem('novax_gh_repo', repo);
  localStorage.setItem('novax_gh_branch', branch);

  const activeVer = localStorage.getItem('novax_hot_version') || CURRENT_APP_VERSION;
  setUpdateLog(`🔍 Checking GitHub for updates (${repo}@${branch})...`, false);

  const rawUrl = `https://raw.githubusercontent.com/${repo}/${branch}/pwa/version.json?t=${Date.now()}`;

  try {
    const res = await fetch(rawUrl, { cache: 'no-store' });
    if (!res.ok) throw new Error(`HTTP ${res.status}: version.json not found on ${branch} branch`);
    const data = await res.json();

    const remoteVer = (data.version || '').trim();
    $('remoteAppVer').textContent = `v${remoteVer}`;

    if (remoteVer && remoteVer !== activeVer) {
      setUpdateLog(`✨ New version found: v${remoteVer} (Installed: v${activeVer})`);
      setUpdateLog(`Changelog: ${data.changelog || 'Latest features & optimizations'}`);
      showUpdateBanner(remoteVer, data.changelog);
      $('hotUpdateBtn').classList.add('pulse-glow');
      return data;
    } else {
      setUpdateLog(`✅ Up to date! You are running the latest version (v${activeVer}).`);
      if (interactive) alert(`NovaX is up to date (v${activeVer})!`);
      return null;
    }
  } catch (err) {
    setUpdateLog(`❌ Update check failed: ${err.message}`);
    if (interactive) alert(`GitHub update check error: ${err.message}\nMake sure your phone has internet and repo name is correct.`);
    return null;
  }
}

async function hotUpdateApp() {
  vibrate(30);
  const { repo, branch } = getGitHubConfig();
  if (!repo) {
    alert('Please enter your GitHub repository (e.g. username/NovaX_PWA_OTA_V2).');
    return;
  }

  const baseUrl = `https://raw.githubusercontent.com/${repo}/${branch}/pwa`;
  setUpdateLog('🚀 Starting Hot Update from GitHub...', false);

  try {
    // 1. Fetch version.json
    setUpdateLog('1/3 Fetching version manifest...');
    const verRes = await fetch(`${baseUrl}/version.json?t=${Date.now()}`, { cache: 'no-store' });
    if (!verRes.ok) throw new Error(`Failed to fetch version.json (${verRes.status})`);
    const verData = await verRes.json();
    const newVer = verData.version || '2.2.0';

    // 2. Fetch style.css
    setUpdateLog('2/3 Downloading latest style.css...');
    const cssRes = await fetch(`${baseUrl}/style.css?t=${Date.now()}`, { cache: 'no-store' });
    if (!cssRes.ok) throw new Error(`Failed to fetch style.css (${cssRes.status})`);
    const cssText = await cssRes.text();
    if (cssText.length < 200) throw new Error('Downloaded style.css is incomplete');

    // 3. Fetch app.js
    setUpdateLog('3/3 Downloading latest app.js...');
    const jsRes = await fetch(`${baseUrl}/app.js?t=${Date.now()}`, { cache: 'no-store' });
    if (!jsRes.ok) throw new Error(`Failed to fetch app.js (${jsRes.status})`);
    const jsText = await jsRes.text();
    if (jsText.length < 200) throw new Error('Downloaded app.js is incomplete');

    // Save into persistent localStorage
    localStorage.setItem('novax_hot_version', newVer);
    localStorage.setItem('novax_hot_css', cssText);
    localStorage.setItem('novax_hot_js', jsText);
    localStorage.setItem('novax_hot_date', new Date().toISOString());

    setUpdateLog(`🎉 Update installed successfully! Reloading NovaX v${newVer}...`);
    vibrate(50);

    setTimeout(() => {
      window.location.reload();
    }, 1200);
  } catch (err) {
    setUpdateLog(`❌ Hot update failed: ${err.message}`);
    alert(`Hot Update Error:\n${err.message}`);
  }
}

function factoryResetApp() {
  vibrate(25);
  if (!confirm('Revert NovaX back to the factory bundled code? This will clear any hot-updated code downloaded from GitHub.')) return;
  localStorage.removeItem('novax_hot_version');
  localStorage.removeItem('novax_hot_css');
  localStorage.removeItem('novax_hot_js');
  localStorage.removeItem('novax_hot_date');
  alert('App reset to factory bundle. Reloading...');
  window.location.reload();
}

function downloadLatestApk() {
  vibrate(20);
  const { repo } = getGitHubConfig();
  if (repo) {
    window.open(`https://github.com/${repo}/releases/latest`, '_blank');
  } else {
    alert('Please enter your GitHub repository first.');
  }
}

// Wire up GitHub Update Buttons
$('checkGhUpdateBtn').onclick = () => checkGithubUpdates(true);
$('hotUpdateBtn').onclick = hotUpdateApp;
$('factoryResetAppBtn').onclick = factoryResetApp;
$('downloadApkBtn').onclick = downloadLatestApk;

$('bannerUpdateBtn').onclick = () => {
  hideUpdateBanner();
  hotUpdateApp();
};
$('bannerDismissBtn').onclick = hideUpdateBanner;

// ================= INITIALIZATION =================
window.addEventListener('DOMContentLoaded', () => {
  $('hostUrlInput').value = base;

  // Initialize GitHub Updater fields
  const savedRepo = localStorage.getItem('novax_gh_repo') || 'onebotyt/robocar';
  const savedBranch = localStorage.getItem('novax_gh_branch') || 'main';
  if ($('ghRepoInput')) $('ghRepoInput').value = savedRepo;
  if ($('ghBranchInput')) $('ghBranchInput').value = savedBranch;

  const activeVer = localStorage.getItem('novax_hot_version') || CURRENT_APP_VERSION;
  $('installedAppVer').textContent = `v${activeVer}`;
  $('appVersionBadge').textContent = `v${activeVer}`;

  const isHot = Boolean(localStorage.getItem('novax_hot_js'));
  const srcTag = $('codeSourceTag');
  if (srcTag) {
    srcTag.textContent = isHot ? 'GitHub Hot-OTA (Active)' : 'Native APK Bundle';
    if (isHot) srcTag.style.color = 'var(--neon-green)';
  }

  loadSavedWifi();
  loadOtaStatus();
  updateStatus();
  setInterval(updateStatus, 1200);

  // Silently check GitHub for updates on startup if internet is available
  if (navigator.onLine && savedRepo) {
    setTimeout(() => checkGithubUpdates(false), 2500);
  }

  // Register service worker for offline app loading
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./sw.js').catch(() => {});
  }
});

