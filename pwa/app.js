/**
 * NovaX Cyber Cockpit - V2 Production Controller
 * Full PWA & Native Capacitor Android Compatible
 * Package: com.novax.controller
 * Primary Communication: RFC 6455 WebSocket (:81) with Dual HTTP REST Fallback
 */

// ================= STATE & CONFIGURATION =================
let base = localStorage.getItem('novaxBase') || 'http://192.168.4.1';
let ws = null;
let wsConnected = false;
let wsReconnectTimer = null;
let wsPingTimer = null;
let lastWsMessageTime = 0;

let isDrawMode = false;
let camOn = false;
let isConnected = false;
let sonarEnabled = true;
let currentMode = 'manual';
let isCarStopped = false;

// DOM Helpers
const $ = (id) => document.getElementById(id);
const $$ = (sel) => document.querySelectorAll(sel);

// Haptic feedback
const vibrate = (ms = 15) => {
  try {
    if (navigator.vibrate) navigator.vibrate(ms);
  } catch (e) {}
};

// ================= PROTOCOL & HTTP REST CLIENT =================
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

// ================= CONNECTION & TELEMETRY INDICATORS =================
function setConnectionState(online, proto = 'HTTP') {
  isConnected = online;
  const dot = $('liveDot');
  if (dot) {
    if (online) dot.classList.add('online');
    else dot.classList.remove('online');
  }

  // Header protocol badge (WS / HTTP / OFF)
  const badge = $('commBadge');
  if (badge) {
    if (online && proto === 'WS') {
      badge.textContent = 'WS';
      badge.className = 'comm-badge ws-live';
      badge.title = 'Real-time WebSocket Connected (:81)';
    } else if (online && proto === 'HTTP') {
      badge.textContent = 'HTTP';
      badge.className = 'comm-badge http-fallback';
      badge.title = 'HTTP Fallback Active (WebSocket Reconnecting...)';
    } else {
      badge.textContent = 'OFF';
      badge.className = 'comm-badge';
      badge.title = 'No Connection to NovaX';
    }
  }

  // Settings modal connection status line
  const statusEl = $('connStatusStat');
  if (statusEl) {
    if (online) {
      statusEl.textContent = `Connected (${proto})`;
      statusEl.className = 'status-green';
    } else {
      statusEl.textContent = 'No connection';
      statusEl.className = 'status-red';
    }
  }
}

// ================= WEBSOCKET CLIENT LAYER (:81) =================
function getWsUrl() {
  try {
    const u = new URL(base);
    const host = u.hostname || '192.168.4.1';
    return `ws://${host}:81/`;
  } catch (e) {
    const cleaned = base.replace(/^https?:\/\//i, '').replace(/\/.*$/, '').split(':')[0];
    return `ws://${cleaned || '192.168.4.1'}:81/`;
  }
}

function initWebSocket() {
  // Clear any existing reconnect timer
  if (wsReconnectTimer) {
    clearTimeout(wsReconnectTimer);
    wsReconnectTimer = null;
  }

  // Gracefully close any existing socket
  if (ws) {
    try {
      ws.onopen = null;
      ws.onmessage = null;
      ws.onerror = null;
      ws.onclose = null;
      ws.close();
    } catch (e) {}
    ws = null;
  }

  const wsUrl = getWsUrl();
  console.log(`[NovaX-WS] Connecting to ${wsUrl}...`);

  try {
    ws = new WebSocket(wsUrl);
  } catch (err) {
    console.warn('[NovaX-WS] Failed to construct WebSocket:', err);
    scheduleWsReconnect();
    return;
  }

  ws.onopen = () => {
    console.log('[NovaX-WS] Connection established on port 81!');
    wsConnected = true;
    lastWsMessageTime = Date.now();
    setConnectionState(true, 'WS');

    // Start keepalive heartbeat ping every 3000ms
    clearInterval(wsPingTimer);
    wsPingTimer = setInterval(() => {
      if (wsConnected) {
        sendWs({ type: 'ping' });
      }
    }, 3000);
  };

  ws.onmessage = (event) => {
    lastWsMessageTime = Date.now();
    setConnectionState(true, 'WS');

    try {
      const data = JSON.parse(event.data);
      handleWsPayload(data);
    } catch (e) {
      console.warn('[NovaX-WS] Non-JSON payload received:', event.data);
    }
  };

  ws.onerror = (err) => {
    console.warn('[NovaX-WS] Error encountered:', err);
  };

  ws.onclose = () => {
    console.log('[NovaX-WS] Disconnected.');
    wsConnected = false;
    clearInterval(wsPingTimer);
    // If HTTP was connected previously, downgrade indicator to HTTP fallback
    if (isConnected) {
      setConnectionState(true, 'HTTP');
    } else {
      setConnectionState(false);
    }
    scheduleWsReconnect();
  };
}

function scheduleWsReconnect() {
  if (wsReconnectTimer) return;
  wsReconnectTimer = setTimeout(() => {
    wsReconnectTimer = null;
    initWebSocket();
  }, 1800);
}

function sendWs(payload) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    try {
      ws.send(typeof payload === 'string' ? payload : JSON.stringify(payload));
      return true;
    } catch (e) {
      console.warn('[NovaX-WS] Send failed:', e);
    }
  }
  return false;
}

// ================= AUTO MODE UI LOCK & STOP/START STATE =================
function applyAutoModeUi(isAuto) {
  // Movement and manual controls that MUST be disabled in Auto mode
  const disabledSelectors = [
    '#dpadUp', '#dpadDown', '#dpadLeft', '#dpadRight', '#stop',
    '#drawModeBtn', '#clearBtn', '#sendPathBtn', '#drawCanvas',
    '#rotLBtn', '#rotRBtn', '#rot360Btn',
    '#scanBtn', '#servoLeftBtn', '#servoRightBtn'
  ];

  disabledSelectors.forEach(sel => {
    const el = document.querySelector(sel);
    if (!el) return;
    if (isAuto) {
      el.classList.add('auto-locked');
      el.setAttribute('disabled', 'true');
      el.setAttribute('tabindex', '-1');
    } else {
      el.classList.remove('auto-locked');
      el.removeAttribute('disabled');
      el.removeAttribute('tabindex');
    }
  });

  const modeBtn = $('modeToggleBtn');
  if (modeBtn) {
    if (isAuto) {
      modeBtn.textContent = 'AUTO';
      modeBtn.className = 'hero-btn active-auto-mode';
      modeBtn.title = 'Car in Autonomous Navigation Mode. Tap to switch to MANUAL.';
    } else {
      modeBtn.textContent = 'MANUAL';
      modeBtn.className = 'hero-btn hero-blue';
      modeBtn.title = 'Car in Manual Driver Mode. Tap to switch to AUTO.';
    }
  }
}

function setCarStoppedState(stopped) {
  isCarStopped = stopped;
  const btn = $('centerStopBtn');
  if (btn) {
    if (stopped) {
      btn.textContent = 'START';
      btn.className = 'hero-btn hero-green';
      btn.title = currentMode === 'auto' ? 'Resume Autonomous Driving' : 'Start Motors / Drive Ready';
    } else {
      btn.textContent = 'STOP';
      btn.className = 'hero-btn hero-red';
      btn.title = 'Emergency Stop';
    }
  }
}

async function startCar() {
  vibrate(25);
  setCarStoppedState(false);
  const sent = sendWs({ type: 'start' });
  if (!sent) {
    await api('/start', { method: 'POST' }).catch(() => {});
  }
}

// Inbound WebSocket Message Router
function handleWsPayload(data) {
  if (!data || !data.type) return;

  switch (data.type) {
    case 'telemetry':
      // Running ESP32 firmware version
      if (data.version) {
        if ($('fwVerHeader')) $('fwVerHeader').textContent = `ESP v${data.version}`;
        if ($('fwVersionVal')) $('fwVersionVal').textContent = `v${data.version}`;
      }

      // Stopped state
      if (data.stopped !== undefined) {
        const stopped = (data.stopped === true || data.stopped === 1 || data.stopped === 'true');
        setCarStoppedState(stopped);
      }

      // Robot mode (manual / auto)
      if (data.mode) {
        currentMode = data.mode === 'auto' ? 'auto' : 'manual';
        applyAutoModeUi(currentMode === 'auto');
      }

      // Ultrasonic Front Distance
      if (data.distance !== undefined && $('radarDistVal')) {
        $('radarDistVal').textContent = data.distance;
      }

      // Battery voltage / status
      const bat = $('batteryStat');
      if (bat && data.battery !== undefined) {
        bat.textContent = `${Number(data.battery).toFixed(2)}V`;
      }

      // Wi-Fi info in settings
      if (data.wifiMode && $('wifiModeVal')) $('wifiModeVal').textContent = data.wifiMode;
      if (data.ip && $('wifiIpVal')) $('wifiIpVal').textContent = data.ip;
      break;

    case 'status':
      if (data.version) {
        if ($('fwVerHeader')) $('fwVerHeader').textContent = `ESP v${data.version}`;
        if ($('fwVersionVal')) $('fwVersionVal').textContent = `v${data.version}`;
      }
      break;

    case 'stopped':
      setCarStoppedState(true);
      break;

    case 'started':
      setCarStoppedState(false);
      break;

    case 'mode':
      if (data.value) {
        currentMode = data.value === 'auto' ? 'auto' : 'manual';
        applyAutoModeUi(currentMode === 'auto');
      }
      break;

    case 'radar':
      // Real-time angle & distance stream during sweep
      if (data.angle !== undefined && data.distance !== undefined) {
        const angle = Number(data.angle);
        const dist = Number(data.distance) || 400;
        const calcBottom = (d) => `${Math.min(85, Math.max(15, (d / 400) * 80))}%`;

        if (angle <= -30 && $('blipLeft')) {
          $('blipLeft').style.bottom = calcBottom(dist);
        } else if (angle >= 30 && $('blipRight')) {
          $('blipRight').style.bottom = calcBottom(dist);
        } else if ($('blipFront')) {
          $('blipFront').style.bottom = calcBottom(dist);
          if ($('radarDistVal')) $('radarDistVal').textContent = dist;
        }
      }
      break;

    case 'radar_summary':
      // Full sweep completed
      if ($('radarStatusTag')) $('radarStatusTag').textContent = 'DONE';
      const L = Number(data.left) || 400;
      const F = Number(data.front) || 400;
      const R = Number(data.right) || 400;

      if ($('radarDistVal')) $('radarDistVal').textContent = F;
      const calcBottom = (d) => `${Math.min(85, Math.max(15, (d / 400) * 80))}%`;
      if ($('blipLeft')) $('blipLeft').style.bottom = calcBottom(L);
      if ($('blipFront')) $('blipFront').style.bottom = calcBottom(F);
      if ($('blipRight')) $('blipRight').style.bottom = calcBottom(R);
      break;

    case 'pong':
      // Heartbeat acknowledgment
      break;

    case 'servo_pos':
      if (data.angle !== undefined) currentServoAngle = Number(data.angle);
      if (data.distance !== undefined && $('radarDistVal')) {
        $('radarDistVal').textContent = data.distance;
      }
      break;
  }
}

// ================= PERIODIC STATUS CHECK (HTTP REST FALLBACK) =================
async function updateStatus() {
  // If WebSocket is actively receiving messages, reduce HTTP poll frequency
  if (wsConnected && (Date.now() - lastWsMessageTime < 5000)) {
    return;
  }

  try {
    const data = await jsonApi('/status');
    setConnectionState(true, wsConnected ? 'WS' : 'HTTP');

    // Distance in Radar
    if (data.distance !== undefined && data.distance !== null && $('radarDistVal')) {
      $('radarDistVal').textContent = data.distance;
    }

    // Battery display
    const bat = $('batteryStat');
    if (bat) {
      if (data.battery !== undefined) bat.textContent = `${Number(data.battery).toFixed(2)}V`;
      else bat.textContent = '98%';
    }

    // Robot mode
    currentMode = (data.mode === 'auto' || data.mode === 0 || data.manual === 0) ? 'auto' : 'manual';
    applyAutoModeUi(currentMode === 'auto');

    // Stopped state
    if (data.stopped !== undefined) {
      setCarStoppedState(data.stopped === true || data.stopped === 1);
    }

    // Wi-Fi Info in Settings
    if (data.wifiMode && $('wifiModeVal')) $('wifiModeVal').textContent = data.wifiMode;
    if (data.ip && $('wifiIpVal')) $('wifiIpVal').textContent = data.ip;

    // ESP32 Running Firmware Version
    if (data.version || data.firmware) {
      const ver = data.version || data.firmware;
      if ($('fwVersionVal')) $('fwVersionVal').textContent = `v${ver}`;
      if ($('fwVerHeader')) $('fwVerHeader').textContent = `ESP v${ver}`;
    }
  } catch (err) {
    if (!wsConnected) {
      setConnectionState(false);
    }
  }
}

// ================= DRIVE CONTROLS (D-PAD & MOTOR WATCHDOG) =================
let moveInterval = null;
let currentMoveCmd = 'S';

async function sendMove(cmd, speed = 180) {
  // If in AUTO mode, lock user driving controls completely
  if (currentMode === 'auto') return;

  // When car is in STOPPED state, driving is blocked until START button is pressed!
  if (isCarStopped && cmd !== 'S') return;

  currentMoveCmd = cmd;

  // 1. Primary: Send via WebSocket text frame
  const sent = sendWs({
    type: 'move',
    dir: cmd,
    speed: speed
  });

  // 2. Fallback: If WebSocket is disconnected, send HTTP REST command
  if (!sent) {
    try {
      await api(`/move?d=${cmd}&speed=${speed}`);
    } catch (e) {}
  }
}

$$('[data-move]').forEach((btn) => {
  const dir = btn.dataset.move;

  const startDrive = (e) => {
    if (currentMode === 'auto') return;
    e.preventDefault();
    vibrate(18);
    btn.classList.add('active');
    sendMove(dir);

    // Continuous refresh every 100ms keeps ESP32 400ms watchdog alive
    clearInterval(moveInterval);
    moveInterval = setInterval(() => sendMove(dir), 100);
  };

  const stopDrive = (e) => {
    if (currentMode === 'auto') return;
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

// Emergency Stop / Start Buttons (Dual-Channel: WS + HTTP)
const stopCar = async () => {
  vibrate(35);
  clearInterval(moveInterval);
  currentMoveCmd = 'S';
  setCarStoppedState(true);

  // Primary: WS stop frame
  sendWs({ type: 'stop' });

  // Dual Fallback: Immediate HTTP stop endpoint
  api('/stop', { method: 'POST' }).catch(() => {});
};

if ($('stop')) $('stop').onclick = stopCar;

// Center STOP / START Button Toggle
if ($('centerStopBtn')) {
  $('centerStopBtn').onclick = () => {
    if (isCarStopped) {
      startCar();
    } else {
      stopCar();
    }
  };
}

// Autonomous / Manual Mode Toggle
if ($('modeToggleBtn')) {
  $('modeToggleBtn').onclick = async () => {
    vibrate(20);
    const target = currentMode === 'manual' ? 'auto' : 'manual';

    const sent = sendWs({ type: 'mode', value: target });
    if (!sent) {
      await api(`/mode?set=${target}`).catch(() => {});
    }
    currentMode = target;
    applyAutoModeUi(currentMode === 'auto');
    if (currentMode === 'auto') {
      setCarStoppedState(false);
    }
  };
}

// Tactical Gyroscope Rotations (MPU6050 Assisted)
if ($('rotLBtn')) {
  $('rotLBtn').onclick = () => {
    if (currentMode === 'auto' || isCarStopped) return;
    vibrate(20);
    if (!sendWs({ type: 'rotate', dir: 'left' })) {
      api('/rotate?dir=left').catch(() => {});
    }
  };
}

if ($('rotRBtn')) {
  $('rotRBtn').onclick = () => {
    if (currentMode === 'auto' || isCarStopped) return;
    vibrate(20);
    if (!sendWs({ type: 'rotate', dir: 'right' })) {
      api('/rotate?dir=right').catch(() => {});
    }
  };
}

if ($('rot360Btn')) {
  $('rot360Btn').onclick = () => {
    if (currentMode === 'auto' || isCarStopped) return;
    vibrate(25);
    if (!sendWs({ type: 'rotate', dir: '360' })) {
      api('/rotate?dir=360').catch(() => {});
    }
  };
}

// 74HC595 Shift Register LED Effects
$$('[data-led]').forEach((btn) => {
  btn.onclick = async () => {
    vibrate(15);
    $$('[data-led]').forEach((b) => b.classList.remove('active'));
    btn.classList.add('active');
    const eff = btn.dataset.led;

    if (!sendWs({ type: 'led', pattern: eff })) {
      await api(`/led?effect=${eff}`).catch(() => {});
    }
  };
});

// ================= DRAW MODE TOGGLE & CANVAS =================
const canvas = $('drawCanvas');
const ctx = canvas ? canvas.getContext('2d') : null;
let drawing = false;
let pathPoints = [];

function resizeCanvas() {
  if (!canvas || !ctx) return;
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
  if (!canvas || !ctx) return;
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

if (canvas) {
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
}

// Toggle between D-Pad and Canvas
if ($('drawModeBtn')) {
  $('drawModeBtn').onclick = () => {
    if (currentMode === 'auto') return;
    vibrate(20);
    isDrawMode = !isDrawMode;

    const dpadView = $('dpadView');
    const canvasView = $('canvasView');
    const modeTag = $('controlModeTag');
    const btn = $('drawModeBtn');
    const clearBtn = $('clearBtn');
    const sendPathBtn = $('sendPathBtn');

    if (isDrawMode) {
      if (dpadView) dpadView.style.display = 'none';
      if (canvasView) canvasView.style.display = 'block';
      if (modeTag) modeTag.textContent = 'CANVAS';
      if (btn) {
        btn.textContent = 'Drive Mode';
        btn.classList.add('active');
      }
      if (clearBtn) clearBtn.style.display = 'inline-flex';
      if (sendPathBtn) sendPathBtn.style.display = 'inline-flex';
      setTimeout(resizeCanvas, 50);
    } else {
      if (canvasView) canvasView.style.display = 'none';
      if (dpadView) dpadView.style.display = 'flex';
      if (modeTag) modeTag.textContent = 'D-PAD';
      if (btn) {
        btn.textContent = 'Draw Mode';
        btn.classList.remove('active');
      }
      if (clearBtn) clearBtn.style.display = 'none';
      if (sendPathBtn) sendPathBtn.style.display = 'none';
    }
  };
}

if ($('clearBtn')) {
  $('clearBtn').onclick = () => {
    vibrate(15);
    pathPoints = [];
    if (canvas && ctx) {
      const rect = canvas.getBoundingClientRect();
      ctx.clearRect(0, 0, rect.width, rect.height);
    }
  };
}

if ($('sendPathBtn')) {
  $('sendPathBtn').onclick = async () => {
    vibrate(30);
    if (pathPoints.length === 0) {
      alert('Draw a path on the canvas first!');
      return;
    }

    // Format coordinates
    const formattedPoints = pathPoints.map((p) => ({
      x: Number(p.x.toFixed(3)),
      y: Number(p.y.toFixed(3))
    }));

    // 1. Try WebSocket
    const sent = sendWs({
      type: 'path',
      points: formattedPoints
    });

    if (sent) {
      alert('Path sent to NovaX via WebSocket!');
      return;
    }

    // 2. HTTP Fallback
    const body = pathPoints
      .map((p) => `${p.x.toFixed(3)},${p.y.toFixed(3)}`)
      .join(';');

    try {
      await api('/path', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain' },
        body
      });
      alert('Path sent to NovaX via HTTP!');
    } catch (err) {
      alert(`Send path failed: ${err.message}`);
    }
  };
}

// ================= RADAR SWEEPER & SONAR =================
if ($('scanBtn')) {
  $('scanBtn').onclick = async () => {
    if (currentMode === 'auto') return;
    vibrate(30);
    if ($('radarStatusTag')) $('radarStatusTag').textContent = 'SCANNING';

    // 1. Try WebSocket scan command
    const sent = sendWs({ type: 'scan' });

    if (!sent) {
      try {
        await api('/scan');
      } catch (e) {
        if ($('radarStatusTag')) $('radarStatusTag').textContent = 'FAILED';
        return;
      }
    }

    // Fallback timer to query /scanResult if WebSocket summary doesn't arrive
    setTimeout(async () => {
      if ($('radarStatusTag') && $('radarStatusTag').textContent === 'SCANNING') {
        try {
          const res = await jsonApi('/scanResult');
          $('radarStatusTag').textContent = 'DONE';

          const L = Number(res.left) || 400;
          const F = Number(res.front) || 400;
          const R = Number(res.right) || 400;

          if ($('radarDistVal')) $('radarDistVal').textContent = F;
          const calcBottom = (dist) => `${Math.min(85, Math.max(15, (dist / 400) * 80))}%`;
          if ($('blipLeft')) $('blipLeft').style.bottom = calcBottom(L);
          if ($('blipFront')) $('blipFront').style.bottom = calcBottom(F);
          if ($('blipRight')) $('blipRight').style.bottom = calcBottom(R);
        } catch (e) {
          $('radarStatusTag').textContent = 'TIMEOUT';
        }
      }
    }, 1200);
  };
}

// ================= SERVO MOTOR HEAD CONTROLS (<  SCAN  >) =================
let currentServoAngle = 90; // Default center 90° (0° = Left, 180° = Right)

function setServoAngle(angle) {
  if (currentMode === 'auto') return;
  angle = Math.max(0, Math.min(180, angle));
  currentServoAngle = angle;

  // 1. Try WebSocket
  const sent = sendWs({ type: 'servo', angle: currentServoAngle });

  // 2. HTTP Fallback
  if (!sent) {
    api(`/servo?angle=${currentServoAngle}`).catch(() => {});
  }
}

if ($('servoLeftBtn')) {
  $('servoLeftBtn').onclick = () => {
    if (currentMode === 'auto') return;
    vibrate(15);
    setServoAngle(currentServoAngle - 15);
  };
}

if ($('servoRightBtn')) {
  $('servoRightBtn').onclick = () => {
    if (currentMode === 'auto') return;
    vibrate(15);
    setServoAngle(currentServoAngle + 15);
  };
}

// ================= LIVE OV7670 CAMERA MODAL =================
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
  if (!img) return;
  const tempImg = new Image();
  const frameUrl = `${base}/cam.jpg?t=${Date.now()}`;

  tempImg.onload = () => {
    img.src = frameUrl;
    camFrameCount++;
    const now = performance.now();
    if (now - lastFpsTime >= 1000) {
      if ($('camFpsVal')) $('camFpsVal').textContent = camFrameCount;
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

if ($('camToggleBtn')) {
  $('camToggleBtn').onclick = () => {
    camOn = !camOn;
    vibrate(20);
    const btn = $('camToggleBtn');
    const standby = $('camStandby');
    const badge = $('camStateBadge');

    if (camOn) {
      btn.textContent = 'Camera OFF';
      if (badge) {
        badge.textContent = 'LIVE';
        badge.style.color = '#10b981';
      }
      if (standby) standby.classList.remove('active');
      fetchCamFrame();
    } else {
      btn.textContent = 'Camera ON';
      if (badge) {
        badge.textContent = 'OFF';
        badge.style.color = 'var(--text-dim)';
      }
      if (standby) standby.classList.add('active');
      clearTimeout(camTimer);
      if ($('camFpsVal')) $('camFpsVal').textContent = '0';
    }
  };
}

if ($('camSnapBtn')) {
  $('camSnapBtn').onclick = () => {
    vibrate(25);
    const img = $('cam');
    if (!img || !img.src || !camOn) {
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
}

// ================= SETTINGS & OTA MODAL =================
if ($('openSettingsModalBtn')) {
  $('openSettingsModalBtn').onclick = () => {
    if ($('settingsModal')) $('settingsModal').style.display = 'flex';
  };
}

if ($('closeSettingsModal')) {
  $('closeSettingsModal').onclick = () => {
    if ($('settingsModal')) $('settingsModal').style.display = 'none';
  };
}

function applyHost(newHost) {
  if (!newHost) return;
  newHost = newHost.trim().replace(/\/+$/, '');
  if (!newHost.startsWith('http://') && !newHost.startsWith('https://')) {
    newHost = 'http://' + newHost;
  }
  base = newHost;
  localStorage.setItem('novaxBase', base);
  if ($('hostUrlInput')) $('hostUrlInput').value = base;

  // Re-establish WebSocket connection on host change
  initWebSocket();
  updateStatus();
}

if ($('saveHostBtn')) {
  $('saveHostBtn').onclick = () => {
    vibrate(15);
    applyHost($('hostUrlInput').value);
    alert(`Connected to ${base}`);
  };
}

$$('.quick-host-pill').forEach((pill) => {
  pill.onclick = () => {
    vibrate(10);
    applyHost(pill.dataset.ip);
  };
});

// Fullscreen Toggle
if ($('fullscreenBtn')) {
  $('fullscreenBtn').onclick = () => {
    vibrate(15);
    if (!document.fullscreenElement) {
      document.documentElement.requestFullscreen().catch(() => {});
    } else {
      document.exitFullscreen().catch(() => {});
    }
  };
}

// ================= WI-FI NETWORK MANAGEMENT (REST) =================
if ($('scanWifiBtn')) {
  $('scanWifiBtn').onclick = async () => {
    vibrate(25);
    const el = $('wifiNetworksList');
    if (!el) return;
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
}

async function loadSavedWifi() {
  const el = $('savedWifiList');
  if (!el) return;
  try {
    const data = await jsonApi('/wifi/saved');
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
          <button class="pill-btn" style="padding:2px 6px;font-size:0.65rem">${isSel ? 'ACTIVE' : 'Select'}</button>
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

if ($('switchStaBtn')) {
  $('switchStaBtn').onclick = async () => {
    vibrate(25);
    try {
      const res = await jsonApi('/wifi/switchSta');
      applyHost(res.host);
      alert(`Switched to STA mode. Connect phone to "${res.ssid}" then access ${res.host}`);
    } catch (e) {
      alert('STA switch failed. Make sure a saved network is selected.');
    }
  };
}

if ($('switchApBtn')) {
  $('switchApBtn').onclick = async () => {
    vibrate(25);
    try {
      await api('/wifi/switchAp');
      applyHost('http://192.168.4.1');
      alert('Switched to AP mode. Connect phone to "NovaX-Car".');
    } catch (e) {
      alert('AP switch request sent.');
    }
  };
}

// ================= ESP32 FIRMWARE OTA FLASH (REST) =================
if ($('fwUpdateBtn')) {
  $('fwUpdateBtn').onclick = async () => {
    vibrate(35);
    const file = $('fwFileInput')?.files[0];
    if (!file) {
      alert('Select an ESP32 .bin firmware file first.');
      return;
    }
    const token = $('otaTokenInput')?.value.trim() || 'NovaX-OTA-ChangeMe';
    if (!confirm(`Flash "${file.name}" to ESP32?\nAll motors will be safely stopped during update.`)) return;

    const msg = $('fwMsg');
    if (msg) msg.textContent = 'Uploading firmware... Robot stopped.';
    try {
      const res = await api('/ota/update', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/octet-stream',
          'X-NovaX-OTA': token
        },
        body: file,
        timeout: 90000
      });
      if (!res.ok) throw new Error(await res.text());
      if (msg) msg.textContent = 'Upload successful! ESP32 restarting in AP mode...';
      alert('Firmware flashed successfully! ESP32 is rebooting.');
    } catch (err) {
      if (msg) msg.textContent = `OTA Error: ${err.message}`;
      alert(`OTA Error: ${err.message}`);
    }
  };
}

// 1-Tap Cloud OTA Flashing from GitHub
async function flashCloudOta() {
  vibrate(30);
  const msg = $('cloudOtaProgress') || $('fwMsg');
  if (msg) {
    msg.style.display = 'block';
    msg.textContent = '1/4 Verifying connection to NovaX car...';
  }

  // Ensure robot is reachable
  try {
    await api('/status', { timeout: 3000 });
  } catch (e) {
    const proceed = confirm(`Car not responding at ${base}.\nEnsure your phone is connected to "NovaX-Car" Wi-Fi.\nProceed anyway?`);
    if (!proceed) {
      if (msg) msg.style.display = 'none';
      return;
    }
  }

  const { repo, branch } = getGitHubConfig();
  const token = $('otaTokenInput')?.value.trim() || 'NovaX-OTA-ChangeMe';

  if (!confirm(`Download latest ESP32 firmware binary directly from GitHub (${repo}) and flash to car?\n\nCar motors will be safely stopped.`)) {
    if (msg) msg.style.display = 'none';
    return;
  }

  try {
    // Step 1: Safety stop
    if (msg) msg.textContent = '2/4 Stopping car motors for safety...';
    try { await stopCar(); } catch (e) {}

    // Step 2: Fetch binary from GitHub raw
    if (msg) msg.textContent = `3/4 Downloading firmware from GitHub (${repo})...`;
    const binUrl = `https://raw.githubusercontent.com/${repo}/${branch}/firmware/NovaX-Firmware.bin?t=${Date.now()}`;
    const binRes = await fetch(binUrl, { cache: 'no-store' });
    if (!binRes.ok) {
      throw new Error(`Failed to download firmware binary from GitHub (HTTP ${binRes.status}). Ensure NovaX-Firmware.bin is pushed to GitHub.`);
    }
    const binBlob = await binRes.blob();
    if (binBlob.size < 50000) {
      throw new Error(`Firmware file too small (${binBlob.size} bytes). Make sure NovaX-Firmware.bin was compiled.`);
    }

    // Step 3: Flash to ESP32
    if (msg) msg.textContent = `4/4 Flashing ${Math.round(binBlob.size / 1024)} KB to ESP32... Do not power off!`;
    const uploadRes = await api('/ota/update', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/octet-stream',
        'X-NovaX-OTA': token
      },
      body: binBlob,
      timeout: 120000
    });

    if (!uploadRes.ok) {
      const errText = await uploadRes.text();
      throw new Error(errText || `HTTP ${uploadRes.status}`);
    }

    if (msg) msg.textContent = '✅ Firmware flashed successfully! ESP32 restarting...';
    alert('🎉 ESP32 Firmware flashed successfully from GitHub!\nNovaX is rebooting now.');
  } catch (err) {
    console.error('Cloud OTA Error:', err);
    if (msg) msg.textContent = `❌ Cloud OTA Failed: ${err.message}`;
    alert(`Cloud OTA Error: ${err.message}`);
  }
}

// ================= GITHUB APP UPDATER (WITH INTEGRITY CHECK & ROLLBACK) =================
const CURRENT_APP_VERSION = '2.3.22';

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
    if ($('remoteAppVer')) $('remoteAppVer').textContent = `v${remoteVer}`;

    // Display Firmware version on GitHub
    if (data.firmware && data.firmware.version && $('ghFwVersionVal')) {
      $('ghFwVersionVal').textContent = `v${data.firmware.version}`;
    }

    if (remoteVer && remoteVer !== activeVer) {
      setUpdateLog(`New version available: v${remoteVer} (Current: v${activeVer})`);
      if ($('appUpdateBanner')) $('appUpdateBanner').style.display = 'flex';
      if ($('updateBannerTitle')) $('updateBannerTitle').textContent = `NovaX v${remoteVer} Ready`;
      if (data.changelog && $('updateBannerDesc')) $('updateBannerDesc').textContent = data.changelog;
      return data;
    } else {
      setUpdateLog(`App is up to date (v${activeVer})`);
      if (interactive) alert(`NovaX Controller is up to date (v${activeVer})!`);
      return null;
    }
  } catch (err) {
    setUpdateLog(`Update check failed: ${err.message}`);
    if (interactive) alert(`Update check error: ${err.message}`);
    return null;
  }
}

async function hotUpdateApp(force = false) {
  vibrate(30);
  const { repo, branch } = getGitHubConfig();
  const baseUrl = `https://raw.githubusercontent.com/${repo}/${branch}/pwa`;
  const rootUrl = `https://raw.githubusercontent.com/${repo}/${branch}`;
  setUpdateLog(force ? 'Forcing live app sync from GitHub...' : 'Starting verified app update from GitHub...', false);

  try {
    // 1. Fetch version.json
    setUpdateLog('1/4 Fetching version metadata...');
    const verRes = await fetch(`${baseUrl}/version.json?t=${Date.now()}`, { cache: 'no-store' });
    if (!verRes.ok) throw new Error(`Failed to fetch version.json: HTTP ${verRes.status}`);
    const verData = await verRes.json();
    const newVer = (verData.version || '').trim() || (force ? 'latest' : '');
    if (!newVer && !force) throw new Error('Invalid version in remote metadata');

    // 2. Download and validate HTML (Index)
    setUpdateLog('2/4 Downloading and validating UI layout (index.html)...');
    let htmlRes = await fetch(`${rootUrl}/index.html?t=${Date.now()}`, { cache: 'no-store' });
    if (!htmlRes.ok) {
      htmlRes = await fetch(`${baseUrl}/index.html?t=${Date.now()}`, { cache: 'no-store' });
    }
    if (!htmlRes.ok) throw new Error(`Failed to fetch index.html: HTTP ${htmlRes.status}`);
    const fullHtml = await htmlRes.text();
    
    // Extract #app container markup
    let appMarkup = '';
    const startIdx = fullHtml.indexOf('<div id="app"');
    const scriptIdx = fullHtml.indexOf('<!-- Dynamic In-App Hot JS Loader');
    if (startIdx !== -1 && scriptIdx !== -1) {
      appMarkup = fullHtml.substring(startIdx, scriptIdx).trim();
    }

    if (!appMarkup || appMarkup.length < 500) {
      throw new Error('Downloaded UI template is incomplete or missing #app container.');
    }
    if ((!appMarkup.includes('cockpit-container') && !appMarkup.includes('cockpit-grid')) || !appMarkup.includes('dpadUp')) {
      throw new Error('Integrity check failed: missing required cockpit elements.');
    }

    // 3. Download and validate CSS
    setUpdateLog('3/4 Downloading style.css...');
    const cssRes = await fetch(`${baseUrl}/style.css?t=${Date.now()}`, { cache: 'no-store' });
    if (!cssRes.ok) throw new Error(`Failed to fetch style.css: HTTP ${cssRes.status}`);
    const cssText = await cssRes.text();
    if (cssText.length < 500) throw new Error('Downloaded CSS payload too small or truncated');

    // 4. Download and strictly validate JS
    setUpdateLog('4/4 Downloading and verifying app.js...');
    const jsRes = await fetch(`${baseUrl}/app.js?t=${Date.now()}`, { cache: 'no-store' });
    if (!jsRes.ok) throw new Error(`Failed to fetch app.js: HTTP ${jsRes.status}`);
    const jsText = await jsRes.text();
    if (jsText.length < 3000) throw new Error('Downloaded JS payload too small or truncated');

    // Integrity check: verify required NovaX controller symbols
    const requiredTokens = ['sendMove', 'stopCar', 'initWebSocket', 'NovaX'];
    for (const token of requiredTokens) {
      if (!jsText.includes(token)) {
        throw new Error(`Integrity check failed: missing required token '${token}'`);
      }
    }

    // Syntax validation check (compile without execution)
    try {
      new Function(jsText);
    } catch (syntaxErr) {
      throw new Error(`Syntax verification failed: ${syntaxErr.message}`);
    }

    // Validation passed: save all hot update layers to localStorage
    localStorage.setItem('novax_hot_version', newVer);
    localStorage.setItem('novax_hot_html', appMarkup);
    localStorage.setItem('novax_hot_css', cssText);
    localStorage.setItem('novax_hot_js', jsText);

    setUpdateLog(`Validation passed! Reloading NovaX v${newVer}...`);
    vibrate(40);
    setTimeout(() => window.location.reload(), 800);
  } catch (err) {
    setUpdateLog(`Update aborted for safety: ${err.message}`);
    alert(`Update Error: ${err.message}\nKeeping current working version.`);
  }
}

function factoryResetApp() {
  vibrate(25);
  if (!confirm('Revert back to factory bundled APK version?\nThis will clear any downloaded hot updates.')) return;
  try {
    localStorage.removeItem('novax_hot_version');
    localStorage.removeItem('novax_hot_html');
    localStorage.removeItem('novax_hot_css');
    localStorage.removeItem('novax_hot_js');
  } catch (e) {}
  alert('Reset to bundle complete. Reloading...');
  window.location.reload();
}

if ($('checkGhUpdateBtn')) $('checkGhUpdateBtn').onclick = () => checkGithubUpdates(true);
if ($('hotUpdateBtn')) $('hotUpdateBtn').onclick = () => hotUpdateApp(false);
if ($('forceUpdateBtn')) $('forceUpdateBtn').onclick = () => hotUpdateApp(true);
if ($('factoryResetAppBtn')) $('factoryResetAppBtn').onclick = factoryResetApp;
if ($('cloudOtaBtn')) $('cloudOtaBtn').onclick = flashCloudOta;
if ($('downloadApkBtn')) {
  $('downloadApkBtn').onclick = () => {
    const { repo } = getGitHubConfig();
    window.open(`https://github.com/${repo}/releases/latest`, '_blank');
  };
}

if ($('bannerUpdateBtn')) {
  $('bannerUpdateBtn').onclick = () => {
    if ($('appUpdateBanner')) $('appUpdateBanner').style.display = 'none';
    hotUpdateApp(false);
  };
}
if ($('bannerDismissBtn')) {
  $('bannerDismissBtn').onclick = () => {
    if ($('appUpdateBanner')) $('appUpdateBanner').style.display = 'none';
  };
}

// ================= APP INITIALIZATION =================
window.addEventListener('DOMContentLoaded', () => {
  if ($('hostUrlInput')) $('hostUrlInput').value = base;

  const savedRepo = localStorage.getItem('novax_gh_repo') || 'onebotyt/robocar';
  if ($('ghRepoInput')) $('ghRepoInput').value = savedRepo;

  const activeVer = localStorage.getItem('novax_hot_version') || CURRENT_APP_VERSION;
  if ($('installedAppVer')) $('installedAppVer').textContent = `v${activeVer}`;

  // 1. Initialize Primary WebSocket connection (:81)
  initWebSocket();

  // 2. Load saved Wi-Fi networks & initial HTTP status
  loadSavedWifi();
  updateStatus();
  setInterval(updateStatus, 2000);

  // 3. Handle window resizing for Canvas Draw Mode
  window.addEventListener('resize', () => {
    if (isDrawMode) resizeCanvas();
  });

  // 4. Check GitHub updates if internet is available
  if (navigator.onLine && savedRepo) {
    setTimeout(() => checkGithubUpdates(false), 2500);
  }

  // 5. Register Service Worker for offline PWA caching
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('./sw.js').catch(() => {});
  }
});
