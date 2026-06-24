#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config.h"
#include "ipixel.h"
#include "lcd_screen.h"
#include "phone_assets.h"
#include "pitch_outcomes.h"
#include "web_index.h"

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

// Outcome lookup lives in pitch_outcomes.h / pitch_outcomes.cpp.

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

void sendPhoneJpg(const uint8_t *data, size_t length) {
  server.send_P(200, "image/jpeg", reinterpret_cast<const char *>(data), length);
}

void handleRoot() {
  server.send_P(200, "text/html", WEB_INDEX_HTML);
}

#if ENABLE_LCD
static int lcdLayoutScale(int value) {
  return (value * LCD_LAYOUT_SCALE_PCT) / 100;
}

static int lcdLayoutOrigin() {
  const int scaled = lcdLayoutScale(SCREEN_H);
  return (SCREEN_H - scaled) / 2;
}

static int lcdLayoutY(int designY) {
  return lcdLayoutOrigin() + lcdLayoutScale(designY);
}

void drawCenteredText(const char *text, int designY, uint16_t color, int size) {
  gfx->setTextColor(color);
  gfx->setTextSize(size);
  int16_t x1, y1;
  uint16_t w, h;
  const int y = lcdLayoutY(designY);
  gfx->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  gfx->setCursor((SCREEN_W - w) / 2, y);
  gfx->print(text);
}
#else
void drawCenteredText(const char *text, int designY, uint16_t color, int size) {
  (void)text;
  (void)designY;
  (void)color;
  (void)size;
}
#endif

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
    case OUTCOME_TRIPLE:
      advanceRunners(3);
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
  const int pitchSpace = pitch.indexOf(' ');
  const int swingSpace = swing.indexOf(' ');
  PlayResult result;

  if (pitchSpace > 0 && swingSpace > 0 &&
      lookupPitchOutcome(
        pitch.substring(0, pitchSpace),
        pitch.substring(pitchSpace + 1),
        swing.substring(0, swingSpace),
        swing.substring(swingSpace + 1),
        result
      )) {
    return result;
  }

  return {"Unable to resolve play.", "waiting", OUTCOME_STRIKE};
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

  lcdScreenInit(gfx);

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
  server.on("/phone_header.jpg", HTTP_GET, []() {
    sendPhoneJpg(PHONE_HEADER_JPG, PHONE_HEADER_JPG_BYTES);
  });
  server.on("/phone_count.jpg", HTTP_GET, []() {
    sendPhoneJpg(PHONE_COUNT_JPG, PHONE_COUNT_JPG_BYTES);
  });
  server.on("/phone_field.jpg", HTTP_GET, []() {
    sendPhoneJpg(PHONE_FIELD_JPG, PHONE_FIELD_JPG_BYTES);
  });
  server.on("/phone_pvp.jpg", HTTP_GET, []() {
    sendPhoneJpg(PHONE_PVP_JPG, PHONE_PVP_JPG_BYTES);
  });
  server.on("/phone_choice.jpg", HTTP_GET, []() {
    sendPhoneJpg(PHONE_CHOICE_JPG, PHONE_CHOICE_JPG_BYTES);
  });
  server.on("/phone_button.jpg", HTTP_GET, []() {
    sendPhoneJpg(PHONE_BUTTON_JPG, PHONE_BUTTON_JPG_BYTES);
  });
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
  lcdScreenStart();

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
  lcdScreenLoop();
  processPendingIPixelResult();
  processPendingIPixelScoreboard();

#if IPIXEL_DIAGNOSTIC_MODE == 2
  runIPixelDiagnosticCycle();
#endif

  ipixelLoop();
}
