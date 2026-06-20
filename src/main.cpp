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

// Team assignment: first phone to join is home, second is away. Tokens are
// client-generated so a page reload reclaims the same team instead of taking a
// new slot.
String homeToken = "";
String awayToken = "";

String pitchValue = "";
String swingValue = "";
String resultText = "";
String currentImage = "waiting";
String pendingIPixelImage = "";
bool pendingIPixelResult = false;
bool pendingIPixelScoreboard = false;
uint32_t lastDiagnosticSlotMs = 0;
uint8_t diagnosticSlotIndex = 0;
uint16_t diagnosticHandle = IPIXEL_DIAG_HANDLE_START;
const uint8_t DIAGNOSTIC_SLOTS[] = {
  IPIXEL_SLOT_HOMERUN,
  IPIXEL_SLOT_SINGLE,
  IPIXEL_SLOT_FOUL,
  IPIXEL_SLOT_LOGO
};

// Outcome of a resolved play: the text shown on the phones and the image token
// used to pick an iPixel animation slot (see showIPixelResult).
enum OutcomeType {
  OUTCOME_HOMERUN,
  OUTCOME_DOUBLE,
  OUTCOME_SINGLE,
  OUTCOME_FOUL,
  OUTCOME_BALL,
  OUTCOME_STRIKE,
  OUTCOME_OUT
};

struct PlayResult {
  String text;
  String image;
  OutcomeType outcome;
};

struct GameState {
  uint8_t inning = 1;
  bool topHalf = true;
  uint8_t homeScore = 0;
  uint8_t awayScore = 0;
  uint8_t balls = 0;
  uint8_t strikes = 0;
  uint8_t outs = 0;
  bool runnerFirst = false;
  bool runnerSecond = false;
  bool runnerThird = false;
  uint8_t awayInningRuns[3] = {0, 0, 0};
  uint8_t homeInningRuns[3] = {0, 0, 0};
};

GameState gameState;

ScoreboardState currentScoreboardState() {
  ScoreboardState state;
  state.inning = gameState.inning;
  state.topHalf = gameState.topHalf;
  state.homeScore = gameState.homeScore;
  state.awayScore = gameState.awayScore;
  state.balls = gameState.balls;
  state.strikes = gameState.strikes;
  state.outs = gameState.outs;
  state.runnerFirst = gameState.runnerFirst;
  state.runnerSecond = gameState.runnerSecond;
  state.runnerThird = gameState.runnerThird;
  for (uint8_t i = 0; i < 3; i++) {
    state.awayInningRuns[i] = gameState.awayInningRuns[i];
    state.homeInningRuns[i] = gameState.homeInningRuns[i];
  }
  return state;
}

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
    button.secondary {
      background: #374151;
      color: #e5e7eb;
    }
    #resultBox {
      margin-top: 12px;
      padding: 16px;
      background: #0b1220;
      border: 1px solid #374151;
      border-radius: 12px;
      text-align: center;
    }
    #resultText {
      margin: 0 0 12px;
      font-size: 1.15rem;
      line-height: 1.35;
    }
    .status {
      min-height: 1.2em;
      color: #9ca3af;
    }
    #scoreboard {
      display: grid;
      gap: 8px;
      margin: 12px 0;
      padding: 12px;
      border: 1px solid #374151;
      border-radius: 12px;
      background: #111827;
    }
    .score-row {
      display: flex;
      justify-content: space-between;
      gap: 12px;
    }
    .bases {
      letter-spacing: 0.08em;
    }
  </style>
</head>
<body>
  <main>
    <h1>Pitch Battle</h1>
    <p class="muted">ESP32 host is running. This is the first local app screen.</p>

    <div id="role">
      <button onclick="joinGame()">Join Game</button>
      <p id="joinMsg" class="muted"></p>
    </div>

    <div id="controls" style="display:none">
      <h2 id="roleTitle"></h2>

      <div id="scoreboard">
        <div class="score-row">
          <strong id="inningText">Top 1</strong>
          <strong id="scoreText">Away 0 - Home 0</strong>
        </div>
        <div class="score-row muted">
          <span id="countText">Count 0-0, 0 out</span>
          <span id="basesText" class="bases">Bases ---</span>
        </div>
      </div>

      <div id="playControls">
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
          <button id="lockPitch" onclick="submitPitch()">Lock Pitch</button>
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
          <button id="lockSwing" onclick="submitSwing()">Lock Swing</button>
        </div>
      </div>

      <p id="status" class="status"></p>

      <div id="resultBox" style="display:none">
        <h2 id="resultText"></h2>
        <button class="secondary" onclick="nextPitch()">Next Pitch</button>
        <button class="secondary" onclick="newGame()">New Game</button>
      </div>
    </div>
  </main>

<script>
let team = "";
let role = "";

function clientId() {
  let id = localStorage.getItem("pitchBattleId");
  if (!id) {
    id = "p-" + Math.random().toString(36).slice(2) + Date.now().toString(36);
    localStorage.setItem("pitchBattleId", id);
  }
  return id;
}

function roleForHalf(half) {
  if (half === "top") {
    return team === "home" ? "pitcher" : "hitter";
  }
  return team === "home" ? "hitter" : "pitcher";
}

async function joinGame() {
  const joinMsg = document.getElementById("joinMsg");
  joinMsg.textContent = "Joining...";
  try {
    const res = await postJson("/api/join", {token: clientId()});
    if (!res || res.team === "full") {
      joinMsg.textContent = "Game is full. Two players have already joined.";
      return;
    }
    team = res.team;
  } catch (err) {
    joinMsg.textContent = "Could not join. Try again.";
    console.log(err);
    return;
  }

  document.getElementById("role").style.display = "none";
  document.getElementById("controls").style.display = "block";
  refreshState();
}

async function postJson(url, data) {
  const res = await fetch(url, {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(data)
  });
  return await res.json();
}

function renderState(state) {
  const statusEl = document.getElementById("status");
  const resultBox = document.getElementById("resultBox");
  const playControls = document.getElementById("playControls");
  const hasResult = state.result && state.pitchLocked && state.swingLocked;
  const halfText = state.half === "top" ? "Top" : "Bottom";

  role = roleForHalf(state.half);
  const teamLabel = team === "home" ? "Home" : "Away";
  const roleLabel = role === "pitcher" ? "Pitching" : "Batting";
  document.getElementById("roleTitle").textContent = `${teamLabel} - ${roleLabel}`;
  document.getElementById("pitcherControls").style.display =
    role === "pitcher" ? "grid" : "none";
  document.getElementById("hitterControls").style.display =
    role === "hitter" ? "grid" : "none";
  const outText = state.outs === 1 ? "out" : "outs";
  const bases =
    (state.runnerFirst ? "1" : "-") +
    (state.runnerSecond ? "2" : "-") +
    (state.runnerThird ? "3" : "-");

  document.getElementById("inningText").textContent =
    `${halfText} ${state.inning}`;
  document.getElementById("scoreText").textContent =
    `Away ${state.awayScore} - Home ${state.homeScore}`;
  document.getElementById("countText").textContent =
    `Count ${state.balls}-${state.strikes}, ${state.outs} ${outText}`;
  document.getElementById("basesText").textContent = `Bases ${bases}`;

  if (hasResult) {
    document.getElementById("resultText").textContent = state.result;
    resultBox.style.display = "block";
    playControls.style.display = "none";
    statusEl.textContent = "";
    return;
  }

  resultBox.style.display = "none";
  playControls.style.display = "block";

  if (state.pitchLocked && state.swingLocked) {
    statusEl.textContent = "Both players locked. Resolving...";
  } else if (state.pitchLocked) {
    statusEl.textContent = role === "pitcher"
      ? "Pitch locked. Waiting for hitter."
      : "Pitcher is ready. Lock your swing.";
  } else if (state.swingLocked) {
    statusEl.textContent = role === "hitter"
      ? "Swing locked. Waiting for pitcher."
      : "Hitter is ready. Lock your pitch.";
  } else {
    statusEl.textContent = "Waiting for both players to lock in.";
  }
}

async function submitPitch() {
  const data = {
    height: document.getElementById("pitchHeight").value,
    speed: document.getElementById("pitchSpeed").value,
    token: clientId()
  };
  const res = await postJson("/api/pitch", data);
  if (res.error) {
    document.getElementById("status").textContent = res.error;
    return;
  }
  renderState(res);
}

async function submitSwing() {
  const data = {
    height: document.getElementById("swingHeight").value,
    timing: document.getElementById("swingTiming").value,
    token: clientId()
  };
  const res = await postJson("/api/swing", data);
  if (res.error) {
    document.getElementById("status").textContent = res.error;
    return;
  }
  renderState(res);
}

async function nextPitch() {
  const res = await fetch("/api/reset", {method: "POST"});
  renderState(await res.json());
}

async function newGame() {
  const res = await fetch("/api/new-game", {method: "POST"});
  renderState(await res.json());
}

async function refreshState() {
  if (!team) return;
  try {
    const res = await fetch("/api/state");
    renderState(await res.json());
  } catch (err) {
    console.log(err);
  }
}

setInterval(refreshState, 1000);
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

uint8_t &battingScore() {
  return gameState.topHalf ? gameState.awayScore : gameState.homeScore;
}

void addBattingInningRun() {
  if (gameState.inning < 1 || gameState.inning > 3) {
    return;
  }

  const uint8_t index = gameState.inning - 1;
  if (gameState.topHalf) {
    gameState.awayInningRuns[index]++;
  } else {
    gameState.homeInningRuns[index]++;
  }
}

void resetCount() {
  gameState.balls = 0;
  gameState.strikes = 0;
}

void clearBases() {
  gameState.runnerFirst = false;
  gameState.runnerSecond = false;
  gameState.runnerThird = false;
}

void advanceHalfInning() {
  gameState.outs = 0;
  resetCount();
  clearBases();

  if (gameState.topHalf) {
    gameState.topHalf = false;
  } else {
    gameState.topHalf = true;
    gameState.inning++;
  }
}

void addOut() {
  gameState.outs++;
  resetCount();

  if (gameState.outs >= 3) {
    advanceHalfInning();
  }
}

void scoreRun() {
  battingScore()++;
  addBattingInningRun();
}

void advanceRunners(uint8_t batterBases) {
  if (batterBases >= 4) {
    if (gameState.runnerFirst) scoreRun();
    if (gameState.runnerSecond) scoreRun();
    if (gameState.runnerThird) scoreRun();
    scoreRun();
    clearBases();
    resetCount();
    return;
  }

  bool nextFirst = false;
  bool nextSecond = false;
  bool nextThird = false;

  if (gameState.runnerThird) scoreRun();

  if (gameState.runnerSecond) {
    if (batterBases >= 2) scoreRun();
    else nextThird = true;
  }

  if (gameState.runnerFirst) {
    if (batterBases >= 3) scoreRun();
    else if (batterBases == 2) nextThird = true;
    else nextSecond = true;
  }

  if (batterBases == 3) nextThird = true;
  if (batterBases == 2) nextSecond = true;
  if (batterBases == 1) nextFirst = true;

  gameState.runnerFirst = nextFirst;
  gameState.runnerSecond = nextSecond;
  gameState.runnerThird = nextThird;
  resetCount();
}

void applyPlayResult(const PlayResult &result) {
  switch (result.outcome) {
    case OUTCOME_HOMERUN:
      advanceRunners(4);
      break;
    case OUTCOME_DOUBLE:
      advanceRunners(2);
      break;
    case OUTCOME_SINGLE:
      advanceRunners(1);
      break;
    case OUTCOME_FOUL:
      if (gameState.strikes < 2) {
        gameState.strikes++;
      }
      break;
    case OUTCOME_BALL:
      gameState.balls++;
      if (gameState.balls >= 4) {
        gameState.balls = 0;
        gameState.strikes = 0;
        advanceRunners(1);
      }
      break;
    case OUTCOME_STRIKE:
      gameState.strikes++;
      if (gameState.strikes >= 3) {
        addOut();
      }
      break;
    case OUTCOME_OUT:
      addOut();
      break;
  }
}

void resetGameState() {
  gameState = GameState();
}

void checkResolve();
String getStateJson();
PlayResult resolvePlay(String pitch, String swing);
String jsonStringValue(const String &body, const char *key);
String teamForToken(const String &token);
String pitchingTeam();
String battingTeam();

void handlePitch() {
  String body = server.arg("plain");
  if (teamForToken(jsonStringValue(body, "token")) != pitchingTeam()) {
    server.send(403, "application/json",
      "{\"error\":\"Not your turn to pitch.\"}");
    return;
  }

  pitchLocked = true;
  pitchValue = jsonStringValue(body, "height") + " " + jsonStringValue(body, "speed");

  Serial.println("Pitch locked:");
  Serial.println(pitchValue);

  checkResolve();

  server.send(200, "application/json", getStateJson());
}

void handleSwing() {
  String body = server.arg("plain");
  if (teamForToken(jsonStringValue(body, "token")) != battingTeam()) {
    server.send(403, "application/json",
      "{\"error\":\"Not your turn to bat.\"}");
    return;
  }

  swingLocked = true;
  swingValue = jsonStringValue(body, "height") + " " + jsonStringValue(body, "timing");

  Serial.println("Swing locked:");
  Serial.println(swingValue);

  checkResolve();

  server.send(200, "application/json", getStateJson());
}

void checkResolve() {
  if (pitchLocked && swingLocked) {
    PlayResult result = resolvePlay(pitchValue, swingValue);
    const uint8_t ballsBefore = gameState.balls;
    const uint8_t strikesBefore = gameState.strikes;

    applyPlayResult(result);
    resultText = result.text;
    currentImage = result.image;

    if (result.outcome == OUTCOME_BALL && ballsBefore == 3) {
      currentImage = "walk";
      resultText = "Walk. Batter takes first base.";
    } else if (result.outcome == OUTCOME_STRIKE && strikesBefore == 2) {
      currentImage = "strikeout";
      resultText = "Strikeout!";
    }

    pendingIPixelImage = currentImage;
    pendingIPixelResult = true;

    Serial.println(resultText);
  }
}

String getStateJson() {
  String json = "{";
  json += "\"pitchLocked\":" + String(pitchLocked ? "true" : "false") + ",";
  json += "\"swingLocked\":" + String(swingLocked ? "true" : "false") + ",";
  json += "\"result\":\"" + resultText + "\",";
  json += "\"image\":\"" + currentImage + "\",";
  json += "\"inning\":" + String(gameState.inning) + ",";
  json += "\"half\":\"" + String(gameState.topHalf ? "top" : "bottom") + "\",";
  json += "\"homeScore\":" + String(gameState.homeScore) + ",";
  json += "\"awayScore\":" + String(gameState.awayScore) + ",";
  json += "\"balls\":" + String(gameState.balls) + ",";
  json += "\"strikes\":" + String(gameState.strikes) + ",";
  json += "\"outs\":" + String(gameState.outs) + ",";
  json += "\"runnerFirst\":" + String(gameState.runnerFirst ? "true" : "false") + ",";
  json += "\"runnerSecond\":" + String(gameState.runnerSecond ? "true" : "false") + ",";
  json += "\"runnerThird\":" + String(gameState.runnerThird ? "true" : "false");
  json += "}";

  return json;
}

PlayResult resolvePlay(String pitch, String swing) {
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
    if (pitchHigh) return {"CRUSHED! Home run to deep left field.", "homerun", OUTCOME_HOMERUN};
    if (pitchMiddle) return {"Solid line drive into center field. Double.", "double", OUTCOME_DOUBLE};
    return {"Hard grounder through the infield. Base hit.", "single", OUTCOME_SINGLE};
  }

  if (score == 3) {
    if (heightDiff == 0) return {"Good contact. Single into the outfield.", "single", OUTCOME_SINGLE};
    return {"Weak contact. Blooper drops in for a single.", "single", OUTCOME_SINGLE};
  }

  if (score == 2) {
    if (pitchFast && swingSlow) return {"Swing and miss. Late on the fastball.", "strike", OUTCOME_STRIKE};
    if (pitchSlow && swingFast) return {"Out in front. Foul ball.", "foul", OUTCOME_FOUL};
    return {"Foul tip. Still alive.", "foul", OUTCOME_FOUL};
  }

  if (score == 1) {
    return {"Bad swing. Routine ground out.", "flyout", OUTCOME_OUT};
  }

  if (heightDiff >= 2) {
    return {"Ball.", "ball", OUTCOME_BALL};
  }

  return {"Strike! Complete miss.", "strike", OUTCOME_STRIKE};
}

void handleReset() {
  pitchLocked = false;
  swingLocked = false;
  pitchValue = "";
  swingValue = "";
  resultText = "";
  currentImage = "waiting";
  pendingIPixelImage = "";
  pendingIPixelResult = false;

  server.send(200, "application/json", getStateJson());
}

void handleNewGame() {
  pitchLocked = false;
  swingLocked = false;
  pitchValue = "";
  swingValue = "";
  resultText = "";
  currentImage = "waiting";
  pendingIPixelImage = "";
  pendingIPixelResult = false;
  pendingIPixelScoreboard = true;
  resetGameState();

  server.send(200, "application/json", getStateJson());
}

void handleState() {
  server.send(200, "application/json", getStateJson());
}

String jsonStringValue(const String &body, const char *key) {
  String needle = String("\"") + key + "\"";
  int k = body.indexOf(needle);
  if (k < 0) return "";
  int colon = body.indexOf(':', k + needle.length());
  if (colon < 0) return "";
  int q1 = body.indexOf('"', colon);
  if (q1 < 0) return "";
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 < 0) return "";
  return body.substring(q1 + 1, q2);
}

String teamForToken(const String &token) {
  if (token.length() == 0) return "";
  if (token == homeToken) return "home";
  if (token == awayToken) return "away";
  return "";
}

// In the top half the home team pitches and away bats; they swap in the bottom.
String pitchingTeam() {
  return gameState.topHalf ? "home" : "away";
}

String battingTeam() {
  return gameState.topHalf ? "away" : "home";
}

void handleJoin() {
  String token = jsonStringValue(server.arg("plain"), "token");

  String team;
  if (token.length() > 0 && token == homeToken) {
    team = "home";
  } else if (token.length() > 0 && token == awayToken) {
    team = "away";
  } else if (homeToken.length() == 0) {
    homeToken = token;
    team = "home";
  } else if (awayToken.length() == 0) {
    awayToken = token;
    team = "away";
  } else {
    team = "full";
  }

  Serial.print("Join request token=");
  Serial.print(token);
  Serial.print(" -> ");
  Serial.println(team);

  server.send(200, "application/json", "{\"team\":\"" + team + "\"}");
}

void processPendingIPixelResult() {
  if (!pendingIPixelResult || ipixelBusy()) {
    return;
  }

  Serial.print("iPixel pending result dispatch: ");
  Serial.println(pendingIPixelImage);
  showIPixelResult(pendingIPixelImage, currentScoreboardState());
  pendingIPixelResult = false;
}

void processPendingIPixelScoreboard() {
  if (!pendingIPixelScoreboard || ipixelBusy()) {
    return;
  }

  Serial.println("iPixel pending live scoreboard dispatch");
  showIPixelScoreboard(currentScoreboardState());
  pendingIPixelScoreboard = false;
}

void runIPixelDiagnosticCycle() {
  if (ipixelBusy() || millis() - lastDiagnosticSlotMs < IPIXEL_DIAG_SLOT_INTERVAL_MS) {
    return;
  }

  lastDiagnosticSlotMs = millis();
  const uint8_t slotCount = sizeof(DIAGNOSTIC_SLOTS) / sizeof(DIAGNOSTIC_SLOTS[0]);
  const uint8_t slot = DIAGNOSTIC_SLOTS[diagnosticSlotIndex % slotCount];
  diagnosticSlotIndex++;

  Serial.print("iPixel diagnostic cycle slot ");
  Serial.println(slot);
  Serial.print("iPixel diagnostic config: mode=");
  Serial.print(IPIXEL_DIAGNOSTIC_MODE);
  Serial.print(" aeChannel=");
  Serial.print(IPIXEL_USE_AE_CHANNEL);
  Serial.print(" writeWithResponse=");
  Serial.print(IPIXEL_WRITE_WITH_RESPONSE);
  Serial.print(" requireSlotAck=");
  Serial.print(IPIXEL_REQUIRE_SLOT_ACK);
  Serial.print(" handleScan=");
  Serial.print(IPIXEL_DIAG_HANDLE_SCAN);
  Serial.print(" rawHandle=0x");
  Serial.println(IPIXEL_RAW_WRITE_HANDLE, HEX);

#if IPIXEL_DIAG_HANDLE_SCAN
  Serial.print("iPixel diagnostic raw handle scan handle=0x");
  Serial.println(diagnosticHandle, HEX);
  ipixelShowSlotAtHandle(slot, diagnosticHandle, IPIXEL_REQUIRE_SLOT_ACK);
  diagnosticHandle++;
  if (diagnosticHandle > IPIXEL_DIAG_HANDLE_END) {
    diagnosticHandle = IPIXEL_DIAG_HANDLE_START;
  }
  return;
#endif

  ipixelShowSlot(slot);
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

#if IPIXEL_DIAGNOSTIC_MODE == 1
  Serial.println("iPixel diagnostic mode 1: BLE-only slot cycling. Wi-Fi disabled.");
  return;
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
  server.on("/api/new-game", HTTP_POST, handleNewGame);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/join", HTTP_POST, handleJoin);
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

#if IPIXEL_DIAGNOSTIC_MODE == 2
  Serial.println("iPixel diagnostic mode 2: Wi-Fi-on idle slot cycling.");
#endif
}

void loop() {
#if IPIXEL_DIAGNOSTIC_MODE == 1
  runIPixelDiagnosticCycle();
  ipixelLoop();
  return;
#endif

  dnsServer.processNextRequest();
  server.handleClient();
  processPendingIPixelResult();
  processPendingIPixelScoreboard();

#if IPIXEL_DIAGNOSTIC_MODE == 2
  runIPixelDiagnosticCycle();
#endif

  ipixelLoop();
}
