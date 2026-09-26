/*
 * ===================================================================================
 *  NOVA-X WIRELESS OTA BOOTSTRAP FLASHER
 * ===================================================================================
 *  Purpose:
 *    Ultra-lightweight sketch to flash compiled firmware (.bin) to ESP32 via Wi-Fi.
 *    Requires ZERO external libraries! Only uses built-in ESP32 core headers.
 *
 *  How to use:
 *    1. Open this sketch in Arduino IDE.
 *    2. Select Board: "ESP32 Dev Module" (or your ESP32 board).
 *    3. In Arduino IDE menu:
 *         Tools -> Partition Scheme -> "Minimal SPIFFS (1.9MB OTA with spiffs)"
 *    4. Upload via USB cable to ESP32.
 *    5. Connect your Phone or PC Wi-Fi to:
 *         SSID:     NovaX-Car
 *         Password: 12345678
 *    6. Open browser at:
 *         http://192.168.4.1
 *    7. Choose "NovaX-Firmware.bin" (from repository /firmware folder) and click Upload!
 * ===================================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

// Wi-Fi Access Point Configuration
const char* AP_SSID = "NovaX-Car";
const char* AP_PASS = "12345678";

WebServer server(80);

// Embedded HTML Flasher Web Page
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>NovaX OTA Bootstrap Flasher</title>
  <style>
    :root {
      --bg: #0b0f19;
      --card: #131b2e;
      --border: #1f2d4a;
      --cyan: #06b6d4;
      --cyan-glow: rgba(6, 182, 212, 0.4);
      --green: #10b981;
      --red: #ef4444;
      --text: #f1f5f9;
      --subtext: #94a3b8;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body {
      background: var(--bg);
      color: var(--text);
      display: flex;
      align-items: center;
      justify-content: center;
      min-height: 100vh;
      padding: 1rem;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 1rem;
      max-width: 480px;
      width: 100%;
      padding: 2rem;
      box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5), 0 0 20px var(--cyan-glow);
    }
    .badge {
      display: inline-block;
      padding: 0.25rem 0.75rem;
      border-radius: 9999px;
      font-size: 0.75rem;
      font-weight: 700;
      letter-spacing: 0.05em;
      text-transform: uppercase;
      background: rgba(6, 182, 212, 0.15);
      color: var(--cyan);
      border: 1px solid var(--cyan);
      margin-bottom: 0.75rem;
    }
    h1 { font-size: 1.5rem; font-weight: 800; margin-bottom: 0.5rem; letter-spacing: -0.02em; }
    p { font-size: 0.875rem; color: var(--subtext); line-height: 1.5; margin-bottom: 1.5rem; }
    .dropzone {
      border: 2px dashed var(--border);
      border-radius: 0.75rem;
      padding: 1.5rem;
      text-align: center;
      margin-bottom: 1.5rem;
      cursor: pointer;
      transition: all 0.2s;
      background: rgba(255, 255, 255, 0.02);
    }
    .dropzone:hover { border-color: var(--cyan); background: rgba(6, 182, 212, 0.05); }
    input[type="file"] { display: none; }
    .file-label { font-size: 0.875rem; color: var(--cyan); font-weight: 600; cursor: pointer; }
    .filename { font-size: 0.8rem; color: var(--text); margin-top: 0.5rem; word-break: break-all; }
    button {
      width: 100%;
      padding: 0.875rem;
      background: linear-gradient(135deg, #06b6d4, #0284c7);
      color: #fff;
      border: none;
      border-radius: 0.75rem;
      font-size: 0.95rem;
      font-weight: 700;
      cursor: pointer;
      transition: all 0.2s;
      box-shadow: 0 4px 15px rgba(6, 182, 212, 0.3);
    }
    button:hover:not(:disabled) { transform: translateY(-1px); box-shadow: 0 6px 20px rgba(6, 182, 212, 0.5); }
    button:disabled { opacity: 0.5; cursor: not-allowed; }
    .progress-box { margin-top: 1.5rem; display: none; }
    .progress-bar-bg {
      background: rgba(255, 255, 255, 0.1);
      border-radius: 9999px;
      height: 10px;
      overflow: hidden;
      margin-bottom: 0.5rem;
    }
    .progress-fill {
      background: linear-gradient(90deg, #06b6d4, #10b981);
      width: 0%;
      height: 100%;
      transition: width 0.15s ease-out;
    }
    .status-text { font-size: 0.8rem; color: var(--subtext); text-align: center; }
    .alert {
      margin-top: 1.25rem;
      padding: 0.75rem 1rem;
      border-radius: 0.5rem;
      font-size: 0.825rem;
      display: none;
    }
    .alert.success { background: rgba(16, 185, 129, 0.15); border: 1px solid var(--green); color: #6ee7b7; display: block; }
    .alert.error { background: rgba(239, 68, 68, 0.15); border: 1px solid var(--red); color: #fca5a5; display: block; }
  </style>
</head>
<body>
  <div class="card">
    <span class="badge">Bootstrap Mode</span>
    <h1>NovaX OTA Flasher</h1>
    <p>Select your compiled <b>NovaX-Firmware.bin</b> to flash wirelessly into the car without needing Arduino libraries.</p>

    <div class="dropzone" onclick="document.getElementById('fwInput').click()">
      <div style="font-size: 2rem; margin-bottom: 0.5rem;">⚡</div>
      <div class="file-label">Choose Firmware Binary (.bin)</div>
      <div class="filename" id="fileChosen">No file chosen</div>
      <input type="file" id="fwInput" accept=".bin">
    </div>

    <button id="flashBtn" disabled onclick="uploadFirmware()">Flash Firmware Now</button>

    <div class="progress-box" id="progressBox">
      <div class="progress-bar-bg">
        <div class="progress-fill" id="progressFill"></div>
      </div>
      <div class="status-text" id="statusText">0% Uploaded</div>
    </div>

    <div class="alert" id="alertBox"></div>
  </div>

  <script>
    const fwInput = document.getElementById('fwInput');
    const fileChosen = document.getElementById('fileChosen');
    const flashBtn = document.getElementById('flashBtn');
    const progressBox = document.getElementById('progressBox');
    const progressFill = document.getElementById('progressFill');
    const statusText = document.getElementById('statusText');
    const alertBox = document.getElementById('alertBox');

    fwInput.onchange = () => {
      if (fwInput.files.length > 0) {
        const file = fwInput.files[0];
        fileChosen.textContent = `${file.name} (${Math.round(file.size / 1024)} KB)`;
        flashBtn.disabled = false;
      } else {
        fileChosen.textContent = 'No file chosen';
        flashBtn.disabled = true;
      }
    };

    // Drag-and-drop support
    const dropzone = document.querySelector('.dropzone');
    ['dragenter', 'dragover'].forEach(evt => {
      dropzone.addEventListener(evt, (e) => {
        e.preventDefault();
        dropzone.style.borderColor = 'var(--cyan)';
        dropzone.style.background = 'rgba(6, 182, 212, 0.08)';
      });
    });
    ['dragleave', 'drop'].forEach(evt => {
      dropzone.addEventListener(evt, (e) => {
        e.preventDefault();
        dropzone.style.borderColor = 'var(--border)';
        dropzone.style.background = 'rgba(255, 255, 255, 0.02)';
      });
    });
    dropzone.addEventListener('drop', (e) => {
      if (e.dataTransfer && e.dataTransfer.files.length) {
        fwInput.files = e.dataTransfer.files;
        fwInput.dispatchEvent(new Event('change'));
      }
    });

    function uploadFirmware() {
      if (!fwInput.files.length) return;
      const file = fwInput.files[0];
      flashBtn.disabled = true;
      progressBox.style.display = 'block';
      alertBox.className = 'alert';
      alertBox.style.display = 'none';

      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/update', true);

      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) {
          const percent = Math.round((e.loaded / e.total) * 100);
          progressFill.style.width = percent + '%';
          statusText.textContent = `Uploading: ${percent}% (${Math.round(e.loaded / 1024)} / ${Math.round(e.total / 1024)} KB)`;
        }
      };

      xhr.onload = () => {
        if (xhr.status === 200) {
          progressFill.style.width = '100%';
          statusText.textContent = 'Flashing Complete! Rebooting car...';
          alertBox.className = 'alert success';
          alertBox.innerHTML = '<b>Update Successful!</b><br>ESP32 is rebooting with NovaX V2 firmware. Reconnect to NovaX-Car in 10 seconds.';
          alertBox.style.display = 'block';
        } else {
          alertBox.className = 'alert error';
          alertBox.textContent = 'Flashing failed! Server returned status ' + xhr.status + ': ' + xhr.responseText;
          alertBox.style.display = 'block';
          flashBtn.disabled = false;
        }
      };

      xhr.onerror = () => {
        alertBox.className = 'alert error';
        alertBox.textContent = 'Network error during upload. Please verify Wi-Fi connection.';
        alertBox.style.display = 'block';
        flashBtn.disabled = false;
      };

      const formData = new FormData();
      formData.append('firmware', file);
      xhr.send(formData);
    }
  </script>
</body>
</html>
)rawliteral";

void setCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("==================================================");
  Serial.println("      NOVA-X WIRELESS OTA BOOTSTRAP FLASHER      ");
  Serial.println("==================================================");

  // Set up Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  IPAddress myIP = WiFi.softAPIP();
  Serial.print("[WIFI] AP SSID:     ");
  Serial.println(AP_SSID);
  Serial.print("[WIFI] AP Password: ");
  Serial.println(AP_PASS);
  Serial.print("[WIFI] AP IP:       http://");
  Serial.println(myIP);
  Serial.println("--------------------------------------------------");
  Serial.println("Open your browser and navigate to: http://192.168.4.1");
  Serial.println("--------------------------------------------------");

  // Handle CORS preflight
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      setCorsHeaders();
      server.send(204);
    } else {
      setCorsHeaders();
      server.send(404, "text/plain", "Not found");
    }
  });

  // Root Web UI & Captive Portal Redirection
  auto sendIndexHtml = []() {
    setCorsHeaders();
    server.send_P(200, "text/html", INDEX_HTML);
  };

  server.on("/", HTTP_GET, sendIndexHtml);
  server.on("/index.html", HTTP_GET, sendIndexHtml);
  server.on("/update", HTTP_GET, sendIndexHtml);
  server.on("/ota/update", HTTP_GET, sendIndexHtml);

  // Captive Portal probes redirect to root
  server.on("/generate_204", HTTP_GET, []() {
    setCorsHeaders();
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    setCorsHeaders();
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "");
  });

  // Status & Identification for NovaX Controller App
  auto sendBootstrapStatus = []() {
    setCorsHeaders();
    server.send(200, "application/json",
      "{\"status\":\"bootstrap\",\"name\":\"NovaX OTA Bootstrap\",\"version\":\"BOOTSTRAP-1.0\",\"ota\":true,\"mode\":\"bootstrap\"}");
  };

  server.on("/status", HTTP_GET, sendBootstrapStatus);
  server.on("/firmware", HTTP_GET, sendBootstrapStatus);
  server.on("/ota/status", HTTP_GET, sendBootstrapStatus);

  // OTA Upload Handlers (Supports both /update and /ota/update)
  auto handleUploadDone = []() {
    setCorsHeaders();
    bool ok = !Update.hasError();
    server.send(
      ok ? 200 : 500,
      "application/json",
      ok ? "{\"status\":\"ok\",\"success\":true,\"restarting\":true,\"message\":\"Firmware flashed successfully! Rebooting...\"}"
         : "{\"status\":\"error\",\"success\":false,\"error\":\"Update failed\"}"
    );

    if (ok) {
      Serial.println("[OTA] Firmware successfully written! Rebooting ESP32 in 1s...");
      delay(1000);
      ESP.restart();
    } else {
      Serial.println("[OTA] Firmware flashing failed!");
    }
  };

  auto handleUploadChunk = []() {
    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[OTA] Flashing started: %s\n", upload.filename.c_str());

      if (Update.isRunning()) {
        Update.abort();
      }

      // Begin update targeting flash application partition (U_FLASH)
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      static unsigned long lastLog = 0;
      if (millis() - lastLog > 500) {
        lastLog = millis();
        Serial.printf("[OTA] Received: %u bytes\n", upload.totalSize);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("[OTA] Write complete! Total size: %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      Update.end();
      Serial.println("[OTA] Upload aborted by client.");
    }
  };

  server.on("/update", HTTP_POST, handleUploadDone, handleUploadChunk);
  server.on("/ota/update", HTTP_POST, handleUploadDone, handleUploadChunk);

  server.begin();
  Serial.println("[HTTP] Web server started on port 80.");
}

void loop() {
  server.handleClient();
  delay(2);
}
