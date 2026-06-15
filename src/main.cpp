#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <qrcode.h>
#include "config.h"
#include "ipixel.h"

#if ENABLE_LCD
  #include <Arduino_GFX_Library.h>

  Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC,
    TFT_CS,
    TFT_SCK,
    TFT_MOSI,
    GFX_NOT_DEFINED
  );

  Arduino_GFX *gfx = new Arduino_GC9A01(
    bus,
    TFT_RST,
    0,
    true
  );
#endif

WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

bool pitchLocked = false;
bool swingLocked = false;

String pitchValue = "";
String swingValue = "";
String resultText = "";
String currentImage = "waiting";

String pageHtml() {
  return R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>Pitch Battle</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body {
      margin: 0;
      font-family: system-ui, sans-serif;
      background: #101827;
      color: white;
      display: grid;
      min-height: 100vh;
      place-items: center;
    }
    main {
      width: min(440px, calc(100% - 32px));
      background: #1f2937;
      border: 1px solid #374151;
      border-radius: 16px;
      padding: 20px;
    }
    h1 { margin-top: 0; }
    button, select {
      width: 100%;
      padding: 14px;
      margin: 8px 0;
      border-radius: 10px;
      border: 1px solid #4b5563;
      font: inherit;
    }
    button {
      background: #2563eb;
      color: white;
      font-weight: 800;
    }
    .muted { color: #9ca3af; }
    .grid { display: grid; gap: 8px; }
  </style>
</head>
<body>
  <main>
    <h1>Pitch Battle</h1>
    <p class="muted">ESP32 host is running. This is the first local app screen.</p>

    <div id="role">
      <button onclick="join('pitcher')">Join as Pitcher</button>
      <button onclick="join('hitter')">Join as Hitter</button>
    </div>

    <div id="controls" style="display:none">
      <h2 id="roleTitle"></h2>

      <div id="pitcherControls" class="grid" style="display:none">
        <select id="pitchHeight">
          <option value="high">High</option>
          <option value="middle">Middle</option>
          <option value="low">Low</option>
        </select>
        <select id="pitchSpeed">
          <option value="fast">Fast</option>
          <option value="medium">Medium</option>
          <option value="slow">Slow</option>
        </select>
        <button onclick="submitPitch()">Lock Pitch</button>
      </div>

      <div id="hitterControls" class="grid" style="display:none">
        <select id="swingHeight">
          <option value="high">High</option>
          <option value="middle">Middle</option>
          <option value="low">Low</option>
        </select>
        <select id="swingTiming">
          <option value="fast">Early / Fast</option>
          <option value="medium">On Time</option>
          <option value="slow">Late / Slow</option>
        </select>
        <button onclick="submitSwing()">Lock Swing</button>
      </div>

      <p id="status" class="muted"></p>
    </div>
  </main>

<script>
let role = "";

function join(nextRole) {
  role = nextRole;
  document.getElementById("role").style.display = "none";
  document.getElementById("controls").style.display = "block";
  document.getElementById("roleTitle").textContent =
    role === "pitcher" ? "Pitcher Controls" : "Hitter Controls";

  document.getElementById("pitcherControls").style.display =
    role === "pitcher" ? "grid" : "none";

  document.getElementById("hitterControls").style.display =
    role === "hitter" ? "grid" : "none";
}

async function postJson(url, data) {
  const res = await fetch(url, {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(data)
  });
  return await res.text();
}

async function submitPitch() {
  const data = {
    height: document.getElementById("pitchHeight").value,
    speed: document.getElementById("pitchSpeed").value
  };
  document.getElementById("status").textContent =
    await postJson("/api/pitch", data);
}

async function submitSwing() {
  const data = {
    height: document.getElementById("swingHeight").value,
    timing: document.getElementById("swingTiming").value
  };
  document.getElementById("status").textContent =
    await postJson("/api/swing", data);
}

setInterval(async () => {
  if (!role) return;

  try {
    const res = await fetch("/api/state");
    const state = await res.json();

    if (state.result) {
      document.getElementById("status").textContent = state.result;
    } else if (state.pitchLocked && state.swingLocked) {
      document.getElementById("status").textContent = "Both players locked.";
    } else if (state.pitchLocked) {
      document.getElementById("status").textContent = "Pitch locked. Waiting for hitter.";
    } else if (state.swingLocked) {
      document.getElementById("status").textContent = "Swing locked. Waiting for pitcher.";
    }
  } catch (err) {
    console.log(err);
  }
}, 1000);
</script>
</body>
</html>
)HTML";
}

void drawCenteredText(const char *text, int y, uint16_t color, int size) {
#if ENABLE_LCD
  gfx->setTextColor(color);
  gfx->setTextSize(size);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx->setCursor((SCREEN_W - w) / 2, y);
  gfx->print(text);
#endif
}

void drawQrCode(const char *url) {
#if ENABLE_LCD
  gfx->fillScreen(BLACK);

  drawCenteredText("PITCH BATTLE", 14, WHITE, 2);
  drawCenteredText("Connect Wi-Fi:", 42, CYAN, 1);
  drawCenteredText(AP_SSID, 56, YELLOW, 1);

  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(3)];
  qrcode_initText(&qrcode, qrcodeData, 3, 0, url);

  int qrSize = qrcode.size;
  int scale = 5;
  int qrPixels = qrSize * scale;
  int startX = (SCREEN_W - qrPixels) / 2;
  int startY = 78;

  gfx->fillRect(startX - 6, startY - 6, qrPixels + 12, qrPixels + 12, WHITE);

  for (uint8_t y = 0; y < qrSize; y++) {
    for (uint8_t x = 0; x < qrSize; x++) {
      uint16_t color = qrcode_getModule(&qrcode, x, y) ? BLACK : WHITE;
      gfx->fillRect(startX + x * scale, startY + y * scale, scale, scale, color);
    }
  }

  drawCenteredText("192.168.4.1", 204, GREEN, 1);
#endif
}

void handleRoot() {
  server.send(200, "text/html", pageHtml());
}

void checkResolve();
String getStateJson();
String resolvePlay(String pitch, String swing);

void handlePitch() {
  pitchLocked = true;
  pitchValue = server.arg("plain");

  Serial.println("Pitch locked:");
  Serial.println(pitchValue);

  checkResolve();

  server.send(200, "application/json", getStateJson());
}

void handleSwing() {
  swingLocked = true;
  swingValue = server.arg("plain");

  Serial.println("Swing locked:");
  Serial.println(swingValue);

  checkResolve();

  server.send(200, "application/json", getStateJson());
}

void checkResolve() {
  if (pitchLocked && swingLocked) {
    resultText = resolvePlay(pitchValue, swingValue);
    if (resultText.indexOf("Home run") >= 0 || resultText.indexOf("CRUSHED") >= 0) {
      currentImage = "homerun";
    } else if (resultText.indexOf("Strike") >= 0 || resultText.indexOf("miss") >= 0) {
      currentImage = "strike";
    } else if (resultText.indexOf("Single") >= 0 || resultText.indexOf("Base hit") >= 0) {
      currentImage = "single";
    } else if (resultText.indexOf("out") >= 0) {
      currentImage = "out";
    } else {
      currentImage = "contact";
    }

    showIPixelResult(currentImage);
    Serial.println(resultText);
  }
}

String getStateJson() {
  String json = "{";
  json += "\"pitchLocked\":" + String(pitchLocked ? "true" : "false") + ",";
  json += "\"swingLocked\":" + String(swingLocked ? "true" : "false") + ",";
  json += "\"result\":\"" + resultText + "\",";
  json += "\"image\":\"" + currentImage + "\"";
  json += "}";

  return json;
}

String resolvePlay(String pitch, String swing) {
  bool pitchHigh = pitch.indexOf("high") >= 0;
  bool pitchMiddle = pitch.indexOf("middle") >= 0;
  bool pitchLow = pitch.indexOf("low") >= 0;

  bool pitchFast = pitch.indexOf("fast") >= 0;
  bool pitchMedium = pitch.indexOf("medium") >= 0;
  bool pitchSlow = pitch.indexOf("slow") >= 0;

  bool swingHigh = swing.indexOf("high") >= 0;
  bool swingMiddle = swing.indexOf("middle") >= 0;
  bool swingLow = swing.indexOf("low") >= 0;

  bool swingFast = swing.indexOf("fast") >= 0;
  bool swingMedium = swing.indexOf("medium") >= 0;
  bool swingSlow = swing.indexOf("slow") >= 0;

  int heightDiff = 0;
  if (pitchHigh && !swingHigh) heightDiff = swingMiddle ? 1 : 2;
  if (pitchMiddle && !swingMiddle) heightDiff = (swingHigh || swingLow) ? 1 : 0;
  if (pitchLow && !swingLow) heightDiff = swingMiddle ? 1 : 2;

  int speedDiff = 0;
  if (pitchFast && !swingFast) speedDiff = swingMedium ? 1 : 2;
  if (pitchMedium && !swingMedium) speedDiff = (swingFast || swingSlow) ? 1 : 0;
  if (pitchSlow && !swingSlow) speedDiff = swingMedium ? 1 : 2;

  int score = 4 - heightDiff - speedDiff;

  if (score >= 4) {
    if (pitchHigh) return "CRUSHED! Home run to deep left field.";
    if (pitchMiddle) return "Solid line drive into center field. Double.";
    return "Hard grounder through the infield. Base hit.";
  }

  if (score == 3) {
    if (heightDiff == 0) return "Good contact. Single into the outfield.";
    return "Weak contact. Blooper drops in for a single.";
  }

  if (score == 2) {
    if (pitchFast && swingSlow) return "Swing and miss. Late on the fastball.";
    if (pitchSlow && swingFast) return "Out in front. Foul ball.";
    return "Foul tip. Still alive.";
  }

  if (score == 1) {
    return "Bad swing. Routine ground out.";
  }

  return "Strike! Complete miss.";
}

void handleReset() {
  pitchLocked = false;
  swingLocked = false;
  pitchValue = "";
  swingValue = "";
  resultText = "Waiting for lock";

  server.send(200, "application/json", getStateJson());
}

void handleState() {
  server.send(200, "application/json", getStateJson());
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("Pitch Battle booting...");
  Serial.flush();

  // Start BLE before LCD init so scanning begins as early as possible.
  ipixelBegin();

#if ENABLE_LCD
  gfx->begin();

  if (TFT_BL >= 0) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  }

  gfx->fillScreen(BLACK);
  if (ipixelLogoDisplayed()) {
    drawCenteredText("iPixel connected", 70, GREEN, 1);
  } else {
    drawCenteredText("iPixel not found", 70, YELLOW, 1);
    drawCenteredText("will retry...", 88, WHITE, 1);
  }

  char seenLine[24];
  snprintf(seenLine, sizeof(seenLine), "BLE devices: %u", ipixelDevicesSeen());
  drawCenteredText(seenLine, 112, CYAN, 1);

  const char *addr = ipixelAddress();
  if (addr[0] != '\0') {
    drawCenteredText("iPixel address:", 140, WHITE, 1);
    drawCenteredText(addr, 156, YELLOW, 1);
    // Hold long enough to read or photograph the address.
    delay(8000);
  } else {
    delay(1500);
  }
#endif

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  ipixelNotifyWifiActive();

  delay(300);

  IPAddress ip = WiFi.softAPIP();

  Serial.println();
  Serial.println("Pitch Battle ESP32 Host");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("URL: ");
  Serial.println(LOCAL_URL);

  dnsServer.start(DNS_PORT, "*", ip);

  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/reset", HTTP_POST, handleReset);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/pitch", HTTP_POST, handlePitch);
  server.on("/api/swing", HTTP_POST, handleSwing);

  server.on("/favicon.ico", HTTP_GET, []() {
    server.send(204);
  });
  
  server.on("/canonical.html", HTTP_GET, handleRoot);
  
  server.on("/ncsi.txt", HTTP_GET, []() {
    server.send(200, "text/plain", "Microsoft NCSI");
  });
  
  server.on("/fwlink", HTTP_GET, handleRoot);
  
  server.on("/success.txt", HTTP_GET, []() {
    server.send(200, "text/plain", "success");
  });

  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  server.on("/connecttest.txt", HTTP_GET, []() {
    server.send(200, "text/plain", "Microsoft Connect Test");
  });

  server.onNotFound([]() {
    Serial.print("404 Not Found: ");
    Serial.print(server.method() == HTTP_GET ? "GET " : "POST ");
    Serial.println(server.uri());
  
    server.send(404, "text/plain", "Not found: " + server.uri());
  });

  server.begin();
  drawQrCode(LOCAL_URL);
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  ipixelLoop();
}
