// Freezy Arena standalone E-stop / A-stop light controller.
// GPIO26: RED relay energized if ANY assigned team E-stopped.
// GPIO27: GREEN relay energized if ANY assigned team A-stopped.
// The two independent HIGH-trigger relays can both energize simultaneously.
// Tested single-channel 5V relay modules set to HIGH trigger. The GPIO
// signals only drive relay IN; relay coils get separate 5V/GND power.
// Subscribe to Freezy Arena's existing Field Monitor WebSocket, no server patch. 

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <cstring>

#include "FieldLightLogic.h"
#include "WebUI.h"

namespace {
constexpr const char *kVersion = "2026.10.22.stops1.0";
constexpr uint16_t kHttpPort = 80;
constexpr uint16_t kArenaPortDefault = 8080;
constexpr uint32_t kServerSilenceMs = 30000;
constexpr uint32_t kArenaStatusStaleMs = 4000;
// Gorilla WebSocket fragments large status messages. A 64 KiB cap protects heap;
// unrelated display messages are discarded before assembly or JSON parsing.
constexpr size_t kMaxWsMessageBytes = 64U * 1024U;
constexpr uint32_t kNoStatusWarningMs = 8000;
constexpr uint32_t kWifiAttemptMs = 12000;
constexpr uint32_t kTestDurationMs = 15000;
constexpr uint32_t kSessionIdleMs = 30UL * 60UL * 1000UL;
constexpr uint32_t kButtonHoldMs = 10000;
constexpr uint32_t kApAfterWifiLostMs = 15000;
constexpr uint8_t kButtonPin = 33;  // Optional NO button to GND, hold 10 s to reset credentials.
constexpr size_t kLogCount = 48;
constexpr char kMatchWsPath[] = "/api/arena/websocket";
constexpr char kFieldWsPath[] = "/displays/field_monitor/websocket";
constexpr const char *kStations[] = {"R1", "R2", "R3", "B1", "B2", "B3"};

struct Settings {
  String ssid;
  String wifiPassword;
  String arenaHost;
  uint16_t arenaPort = kArenaPortDefault;
  uint8_t heartbeatPin = 26;
  uint8_t runningPin = 27;
};

struct EventLine {
  uint32_t atMs = 0;
  String text;
};

Preferences prefs;
WebServer http(kHttpPort);
WebSocketsClient arenaWs;
Settings config;
String wsPath;
String wsFragmentBuffer;
bool wsFragmentActive = false;
bool wsFragmentOverflow = false;
bool wsFragmentIgnored = false;
bool wsProcessingFragmented = false;
uint32_t wsIgnoredMessages = 0;
uint32_t wsStatusParseErrors = 0;
uint32_t wsPeakStatusParseMs = 0;
uint32_t wsFragmentedCount = 0;
uint32_t wsFragmentErrorCount = 0;
uint32_t wsStatusReceivedCount = 0;
uint32_t wsConnectedAtMs = 0;
bool wsMissingStatusWarned = false;
EventLine events[kLogCount];
size_t eventHead = 0;
size_t eventUsed = 0;

String apPassword;
String adminSalt;
String adminHash;
String sessionToken;
String csrfToken;
uint32_t lastSessionActivity = 0;
uint8_t badLogins = 0;
uint32_t loginBlockedUntil = 0;

bool apRunning = false;
bool wsStarted = false;
bool wsConnected = false;
bool haveState = false;
bool haveArenaStatus = false;
uint8_t eStopMask = 0;  // Bits R1, R2, R3, B1, B2, B3
uint8_t aStopMask = 0;
uint8_t teamsAssigned = 0;
uint32_t lastArenaStatusMs = 0;
bool redOn = false;    // Commanded relay GPIO level, not contact feedback
bool greenOn = false;  // HIGH means energized on verified H-trigger modules
bool lastStatusFresh = false;
bool lastStopRed = false;
bool lastStopGreen = false;
int fieldState = -1;
uint32_t lastFrameMs = 0;
uint32_t lastWifiAttempt = 0;
uint32_t wifiLostSince = 0;
uint32_t wifiConnectedSince = 0;
uint32_t apExplicitUntil = 0;
uint32_t wsConnects = 0;
uint32_t wsDrops = 0;
uint32_t buttonDownSince = 0;
bool resetButtonActivated = false;

bool manualActive = false;
field_light::StopRelayRequest manualCommand{false, false};
uint32_t manualUntilMs = 0;
uint32_t rebootAtMs = 0;
bool otaStarted = false;
bool otaSucceeded = false;

void addLog(const String &message) {
  // C++11-compatible update: EventLine has a default member initializer.
  events[eventHead].atMs = millis();
  events[eventHead].text = message;
  eventHead = (eventHead + 1) % kLogCount;
  if (eventUsed < kLogCount) ++eventUsed;
  Serial.printf("[%lus] %s\n", static_cast<unsigned long>(millis() / 1000), message.c_str());
}

String randomString(size_t len) {
  static constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  String value;
  value.reserve(len);
  for (size_t i = 0; i < len; ++i) value += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  return value;
}

String sha256Hex(const String &value) {
  uint8_t digest[32];
  mbedtls_sha256(reinterpret_cast<const unsigned char *>(value.c_str()), value.length(), digest, 0);
  static const char alphabet[] = "0123456789abcdef";
  char chars[65];
  for (size_t i = 0; i < 32; ++i) {
    chars[2 * i] = alphabet[(digest[i] >> 4) & 0xF];
    chars[2 * i + 1] = alphabet[digest[i] & 0xF];
  }
  chars[64] = '\0';
  return String(chars);
}

bool constantTimeEqual(const String &a, const String &b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

bool validPin(int pin) {
  // Safe output GPIOs for this ESP32-WROOM development board. 33 reserved for recovery.
  constexpr int allowed[] = {16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32};
  for (int candidate : allowed) if (candidate == pin) return true;
  return false;
}

bool validHost(const String &host) {
  if (host.isEmpty() || host.length() > 95) return false;
  if (host.startsWith(".") || host.endsWith(".")) return false;
  for (size_t i = 0; i < host.length(); ++i) {
    const char ch = host[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_')) return false;
  }
  return true;
}

void loadConfig() {
  config.ssid = prefs.getString("ssid", "");
  config.wifiPassword = prefs.getString("wifiPass", "");
  config.arenaHost = prefs.getString("host", "");
  config.arenaPort = prefs.getUShort("port", kArenaPortDefault);
  config.heartbeatPin = prefs.getUChar("hbPin", prefs.getUChar("redPin", 26));
  config.runningPin = prefs.getUChar("runPin", prefs.getUChar("greenPin", 27));
  // Migrate settings from the original GPIO relay edition if they exist.
  // New keys take priority; no active-low option on 0-10V PLC signals.
  if (!validPin(config.heartbeatPin) || !validPin(config.runningPin) ||
      config.heartbeatPin == config.runningPin) {
    config.heartbeatPin = 26;
    config.runningPin = 27;
  }
  if (!validHost(config.arenaHost)) config.arenaHost = "";
  if (config.arenaPort == 0) config.arenaPort = kArenaPortDefault;
}

void ensureCredentials() {
  apPassword = prefs.getString("apPass", "");
  adminSalt = prefs.getString("salt", "");
  adminHash = prefs.getString("pwdHash", "");
  if (apPassword.length() < 12 || adminSalt.length() < 12 || adminHash.length() != 64) {
    apPassword = randomString(16);
    const String generatedAdmin = randomString(16);
    adminSalt = randomString(22);
    adminHash = sha256Hex(adminSalt + ":" + generatedAdmin);
    prefs.putString("apPass", apPassword);
    prefs.putString("salt", adminSalt);
    prefs.putString("pwdHash", adminHash);
    Serial.println("\n==========================================================");
    Serial.println("   NEW FIELD LIGHT - WRITE DOWN THESE CREDENTIALS NOW");
    Serial.printf("   Wi-Fi setup AP password: %s\n", apPassword.c_str());
    Serial.printf("   Web management password: %s\n", generatedAdmin.c_str());
    Serial.println("   These web credentials are displayed only when created.");
    Serial.println("   Lost password? Hold GPIO33 to GND for 10 s to reset.");
    Serial.println("==========================================================\n");
  }
}

void invalidateSession() {
  sessionToken = "";
  csrfToken = "";
  lastSessionActivity = 0;
}

void resetCredentials() {
  prefs.remove("apPass");
  prefs.remove("salt");
  prefs.remove("pwdHash");
  invalidateSession();
  addLog("Physical password recovery requested: restarting");
  rebootAtMs = millis() + 600;
}

// H jumper: GPIO HIGH energizes relay, LOW releases it. CMD is NOT ground.
void initializeRelayPins() {
  digitalWrite(config.heartbeatPin, LOW);
  digitalWrite(config.runningPin, LOW);
  pinMode(config.heartbeatPin, OUTPUT);
  pinMode(config.runningPin, OUTPUT);
  redOn = false;
  greenOn = false;
}
void setRelays(bool redEStop, bool greenAStop) {
  if (redOn != redEStop) digitalWrite(config.heartbeatPin, redEStop ? HIGH : LOW);
  if (greenOn != greenAStop) digitalWrite(config.runningPin, greenAStop ? HIGH : LOW);
  redOn = redEStop;
  greenOn = greenAStop;
}

bool freshField() {
  return WiFi.status() == WL_CONNECTED && wsConnected && haveState &&
         static_cast<uint32_t>(millis() - lastFrameMs) < kServerSilenceMs;
}

const char *fieldStateName(int state) {
  static constexpr const char *names[] = {
      "Pre-match", "Starting", "Autonomous", "Auto/teleop pause", "Teleop",
      "Post-match", "Timeout", "Post-timeout"};
  return (state >= 0 && state <= 7) ? names[state] : "Unknown";
}

field_light::FieldPhase fieldPhase() {
  return field_light::phaseForState(fieldState, freshField());
}

bool freshArenaStatus() {
  return WiFi.status() == WL_CONNECTED && wsConnected && haveArenaStatus &&
      static_cast<uint32_t>(millis() - lastArenaStatusMs) < kArenaStatusStaleMs;
}

void clearArenaStatus() {
  haveArenaStatus = false;
  eStopMask = 0;
  aStopMask = 0;
  teamsAssigned = 0;
  lastArenaStatusMs = 0;
}

void stopManualTest(const String &reason) {
  if (!manualActive) return;
  manualActive = false;
  manualUntilMs = 0;
  addLog(reason);
}

void updateOutputs() {
  if (otaStarted || rebootAtMs) {
    setRelays(false, false);
    return;
  }
  if (manualActive && static_cast<int32_t>(millis() - manualUntilMs) >= 0)
    stopManualTest("Stop-light test expired; automatic status restored");
  if (manualActive && fieldPhase() == field_light::FieldPhase::Running)
    stopManualTest("Stop-light test cancelled: match started");

  const bool statusFresh = freshArenaStatus();
  // Invalid or stale FMS data is UNKNOWN, not an asserted team stop.
  const field_light::StopRelayRequest requested = manualActive
      ? manualCommand
      : field_light::StopRelayRequest{statusFresh && eStopMask != 0,
                                      statusFresh && aStopMask != 0};
  if (requested.redEStop != lastStopRed || requested.greenAStop != lastStopGreen ||
      statusFresh != lastStatusFresh) {
    addLog(String("Stops: ") + (statusFresh ? "FMS ONLINE" : "FMS UNKNOWN") +
           ", RED E-stop " + (requested.redEStop ? "ON" : "OFF") +
           ", GREEN A-stop " + (requested.greenAStop ? "ON" : "OFF"));
    lastStopRed = requested.redEStop;
    lastStopGreen = requested.greenAStop;
    lastStatusFresh = statusFresh;
  }
  setRelays(requested.redEStop, requested.greenAStop);
}

// Decode a complete logical text message. A single JSON document may arrive
// across several WebSocket frames, especially arenaStatus, because Freezy Arena
// uses a small Gorilla WebSocket write buffer. Do not process partial JSON.
void processArenaText(const uint8_t *payload, size_t length) {
  // Freezy Arena's field monitor carries frequent realtimeScore and other
  // unrelated display updates. They are skipped before calling this function.
  // Build ArduinoJson's field selector once, not for every 500 ms status tick.
  static JsonDocument filter;
  static bool filterInitialized = false;
  if (!filterInitialized) {
    filter["type"] = true;
    filter["data"]["MatchState"] = true;
    for (const char *station : kStations) {
      filter["data"]["AllianceStations"][station]["Team"]["Id"] = true;
      filter["data"]["AllianceStations"][station]["EStop"] = true;
      filter["data"]["AllianceStations"][station]["AStop"] = true;
    }
    filterInitialized = true;
  }
  const uint32_t parseStartedMs = millis();
  JsonDocument message;
  const auto error = deserializeJson(message, payload, length, DeserializationOption::Filter(filter));
  const uint32_t parseMs = millis() - parseStartedMs;
  if (parseMs > wsPeakStatusParseMs) wsPeakStatusParseMs = parseMs;
  if (error) {
    ++wsStatusParseErrors;
    // Do not fill the log with repeated parse errors while Wi-Fi is saturated.
    if (wsStatusParseErrors <= 3 || (wsStatusParseErrors % 50U) == 0)
      addLog(String("Malformed Arena message: ") + error.c_str() +
             " (" + String(length) + " bytes)");
    return;
  }
  const char *kind = message["type"].as<const char *>();
  if (!kind) return;
  if (strcmp(kind, "matchTime") == 0) {
    if (!message["data"]["MatchState"].is<int>()) {
      haveState = false;
      fieldState = -1;
      addLog("Arena matchTime missing numeric MatchState");
      return;
    }
    const int incoming = message["data"]["MatchState"].as<int>();
    if (incoming < 0 || incoming > 7) {
      haveState = false;
      fieldState = -1;
      addLog("Arena sent unknown MatchState");
      return;
    }
    if (!haveState || fieldState != incoming) {
      addLog(String("Freezy match state: ") + fieldStateName(incoming));
    }
    fieldState = incoming;
    haveState = true;
    lastFrameMs = millis();
  } else if (strcmp(kind, "arenaStatus") == 0) {
    ++wsStatusReceivedCount;
    if (wsStatusReceivedCount == 1) {
      addLog(String("First arenaStatus received: ") + String(length) +
             " bytes" + (wsProcessingFragmented ? " (fragmented)" : " (single frame)"));
    }
    // This message comes from the existing field-monitor WebSocket, which
    // does NOT require match-play administration privileges.
    const JsonVariantConst status = message["data"];
    if (!status["AllianceStations"].is<JsonObjectConst>()) {
      clearArenaStatus();
      addLog("Arena status missing AllianceStations; no stop inferred");
      return;
    }
    field_light::StationStopState stations[6]{};
    uint8_t assigned = 0;
    uint8_t redMask = 0;
    uint8_t greenMask = 0;
    bool valid = true;
    for (size_t i = 0; i < 6; ++i) {
      const JsonVariantConst info = status["AllianceStations"][kStations[i]];
      // Explicit boolean stop values are required; never infer stops from
      // disconnected/unassigned driver stations or missing JSON fields.
      if (info.isNull() || !info["EStop"].is<bool>() || !info["AStop"].is<bool>()) {
        valid = false;
        break;
      }
      stations[i].assigned = (info["Team"]["Id"] | 0) > 0;
      stations[i].eStopped = info["EStop"].as<bool>();
      stations[i].aStopped = info["AStop"].as<bool>();
      if (stations[i].assigned) {
        ++assigned;
        if (stations[i].eStopped) redMask |= uint8_t(1U << i);
        if (stations[i].aStopped) greenMask |= uint8_t(1U << i);
      }
    }
    if (!valid) {
      clearArenaStatus();
      addLog("Arena station data missing EStop or AStop; no stop inferred");
      return;
    }
    const auto stopReq = field_light::stopRelaysForStations(true, stations, 6);
    const bool changed = !haveArenaStatus || redMask != eStopMask ||
                         greenMask != aStopMask;
    eStopMask = redMask;
    aStopMask = greenMask;
    teamsAssigned = assigned;
    haveArenaStatus = true;
    lastArenaStatusMs = millis();
    lastFrameMs = millis();
    if (changed) {
      addLog(String("Arena team stops: RED ") +
             (stopReq.redEStop ? "ACTIVE" : "clear") +
             ", GREEN " + (stopReq.greenAStop ? "ACTIVE" : "clear") +
             " (assigned teams " + String(assigned) + ")");
    }

  } else if (strcmp(kind, "ping") == 0 || strcmp(kind, "matchLoad") == 0 ||
             strcmp(kind, "matchTiming") == 0) {
    lastFrameMs = millis();
  }
}

void resetWsFragments() {
  wsFragmentActive = false;
  wsFragmentIgnored = false;
  wsFragmentOverflow = false;
  wsFragmentBuffer = "";
}

void appendWsFragment(const uint8_t *payload, size_t length) {
  if (!wsFragmentActive || wsFragmentOverflow || wsFragmentIgnored) return;
  if (length > kMaxWsMessageBytes ||
      wsFragmentBuffer.length() > kMaxWsMessageBytes - length ||
      ESP.getFreeHeap() < length + 12000U) {
    ++wsFragmentErrorCount;
    wsFragmentOverflow = true;
    wsFragmentBuffer = "";
    addLog("Arena WS fragment exceeded buffer or available RAM; message discarded");
    return;
  }
  if (length > 0 && !wsFragmentBuffer.concat(reinterpret_cast<const char *>(payload),
                                              static_cast<unsigned int>(length))) {
    ++wsFragmentErrorCount;
    wsFragmentOverflow = true;
    wsFragmentBuffer = "";
    addLog("Arena WS fragment allocation failed; message discarded");
  }
}

// Freezy Arena's websocket.Message serializes {"type":...,"data":...} in
// this order. Identify the type from just the first frame and discard all
// unrelated Field Monitor messages *without* assembling large JSON documents.
// The helper is unit-tested with complete, split, and malformed prefixes.
field_light::WsMessageKind wsKind(const uint8_t *payload, size_t length) {
  return field_light::classifyWsMessage(payload, length);
}

void arenaEvent(WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_CONNECTED) {
    resetWsFragments();
    wsConnected = true;
    haveState = false;
    fieldState = -1;
    clearArenaStatus();
    wsConnectedAtMs = millis();
    wsMissingStatusWarned = false;
    lastFrameMs = millis();
    ++wsConnects;
    addLog("Freezy Arena WebSocket connected; awaiting field data");
  } else if (type == WStype_DISCONNECTED || type == WStype_ERROR) {
    if (type == WStype_ERROR) addLog("Arena WebSocket transport error");
    if (wsConnected || haveState) { ++wsDrops; addLog("Freezy Arena connection lost"); }
    resetWsFragments();
    wsConnected = false;
    haveState = false;
    fieldState = -1;
    clearArenaStatus();
  } else if (type == WStype_TEXT && wsConnected) {
    resetWsFragments();
    const auto kind = wsKind(payload, length);
    if (kind == field_light::WsMessageKind::Ping ||
        kind == field_light::WsMessageKind::OtherHeartbeat) {
      lastFrameMs = millis();
    } else if (kind == field_light::WsMessageKind::ArenaStatus ||
               kind == field_light::WsMessageKind::MatchTime) {
      processArenaText(payload, length);
    } else {
      ++wsIgnoredMessages;
    }
  } else if (type == WStype_FRAGMENT_TEXT_START && wsConnected) {
    resetWsFragments();
    const auto kind = wsKind(payload, length);
    wsFragmentIgnored = kind != field_light::WsMessageKind::ArenaStatus &&
                        kind != field_light::WsMessageKind::MatchTime;
    if (wsFragmentIgnored) {
      ++wsIgnoredMessages;
      if (kind == field_light::WsMessageKind::Ping ||
          kind == field_light::WsMessageKind::OtherHeartbeat) lastFrameMs = millis();
    } else {
      wsFragmentActive = true;
      // Avoid repeated exponential String reallocations as each 2 KB frame arrives.
      if (ESP.getFreeHeap() > 55000U)
        wsFragmentBuffer.reserve(16U * 1024U);
      appendWsFragment(payload, length);
    }
  } else if (type == WStype_FRAGMENT && wsConnected) {
    appendWsFragment(payload, length);
  } else if (type == WStype_FRAGMENT_FIN && wsConnected) {
    if (wsFragmentActive && !wsFragmentOverflow && !wsFragmentIgnored) {
      appendWsFragment(payload, length);
      if (!wsFragmentOverflow) {
        ++wsFragmentedCount;
        wsProcessingFragmented = true;
        processArenaText(reinterpret_cast<const uint8_t *>(wsFragmentBuffer.c_str()),
                         wsFragmentBuffer.length());
        wsProcessingFragmented = false;
      }
    }
    resetWsFragments();
  }
}

void beginArenaWebSocket() {
  if (wsStarted || WiFi.status() != WL_CONNECTED || !validHost(config.arenaHost)) return;
  const uint32_t id = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFULL);
  wsPath = String(kFieldWsPath) + "?displayId=esp32-dualrelay-" +
           String(id, HEX) + "&nickname=ESP32-Dual-Relay";
  addLog(String("Arena subscription path: ") + wsPath);
  arenaWs.begin(config.arenaHost.c_str(), config.arenaPort, wsPath.c_str());
  arenaWs.setExtraHeaders(""); // prevent Origin:file:// rejection by Gorilla
  arenaWs.onEvent(arenaEvent);
  arenaWs.setReconnectInterval(3000);
  wsStarted = true;
  addLog("Connecting to Freezy Arena " + config.arenaHost + ":" +
         String(config.arenaPort) + " (two-relay status)");
}

void runArenaSocket() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wsStarted) { arenaWs.disconnect(); wsStarted = false; }
    wsConnected = false;
    haveState = false;
    fieldState = -1;
    clearArenaStatus();
    return;
  }
  beginArenaWebSocket();
  if (!wsStarted) return;
  arenaWs.loop();
  if (wsConnected && !haveArenaStatus && !wsMissingStatusWarned &&
      static_cast<uint32_t>(millis() - wsConnectedAtMs) > kNoStatusWarningMs) {
    wsMissingStatusWarned = true;
    addLog("No arenaStatus received from Field Monitor: check WS diagnostics / fragmentation counters");
  }
  if (wsConnected && static_cast<uint32_t>(millis() - lastFrameMs) > kServerSilenceMs) {
    addLog("Arena heartbeat timed out; reconnecting");
    arenaWs.disconnect();
    wsStarted = false;
    wsConnected = false;
    haveState = false;
    fieldState = -1;
    clearArenaStatus();
    ++wsDrops;
  }
}

String apName() {
  char buf[29];
  snprintf(buf, sizeof(buf), "Freezy-FieldLight-%06lX",
           static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFFUL));
  return String(buf);
}

void startSetupAp() {
  if (apRunning) return;
  WiFi.mode(WIFI_AP_STA);
  apRunning = WiFi.softAP(apName().c_str(), apPassword.c_str());
  if (apRunning) {
    addLog("Setup Wi-Fi enabled: " + apName() + " / http://192.168.4.1");
    Serial.printf("[setup] AP Wi-Fi password (physical serial only): %s\n", apPassword.c_str());
  } else addLog("ERROR starting setup Wi-Fi");
}

void stopSetupAp() {
  if (!apRunning) return;
  WiFi.softAPdisconnect(false);
  apRunning = false;
  addLog("Setup Wi-Fi disabled (LAN management remains available)");
}

void connectWifi() {
  if (config.ssid.isEmpty()) { startSetupAp(); return; }
  WiFi.begin(config.ssid.c_str(), config.wifiPassword.c_str());
  lastWifiAttempt = millis();
  addLog("Joining configured Wi-Fi: " + config.ssid);
}

void manageWifi() {
  static bool wasConnected = false;
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) {
    wifiLostSince = 0;
    if (!wasConnected) {
      wifiConnectedSince = millis();
      addLog("Wi-Fi connected: " + WiFi.localIP().toString());
      if (MDNS.begin("freezy-fieldlight")) addLog("mDNS: freezy-fieldlight.local");
    }
    // Allow time to read the new STA address, then retire recovery AP unless explicitly requested.
    if (apRunning && wifiConnectedSince && millis() - wifiConnectedSince > 45000 &&
        (!apExplicitUntil || static_cast<int32_t>(millis() - apExplicitUntil) >= 0)) stopSetupAp();
  } else {
    if (wasConnected) addLog("Wi-Fi connection lost");
    wifiConnectedSince = 0;
    if (wifiLostSince == 0) wifiLostSince = millis();
    if (config.ssid.length() && static_cast<uint32_t>(millis() - lastWifiAttempt) > kWifiAttemptMs) {
      WiFi.reconnect();
      lastWifiAttempt = millis();
    }
    if (config.ssid.isEmpty() || static_cast<uint32_t>(millis() - wifiLostSince) > kApAfterWifiLostMs)
      startSetupAp();
  }
  wasConnected = connected;
}

String readCookie(const String &cookie, const String &name) {
  const String wanted = name + "=";
  int start = 0;
  while (start < cookie.length()) {
    const int end = cookie.indexOf(';', start);
    String item = cookie.substring(start, end < 0 ? cookie.length() : end);
    item.trim();
    if (item.startsWith(wanted)) return item.substring(wanted.length());
    if (end < 0) break;
    start = end + 1;
  }
  return "";
}

bool authenticated() {
  if (sessionToken.isEmpty() || static_cast<uint32_t>(millis() - lastSessionActivity) > kSessionIdleMs) {
    invalidateSession();
    return false;
  }
  const String candidate = readCookie(http.header("Cookie"), "FLSESSION");
  if (!constantTimeEqual(candidate, sessionToken)) return false;
  lastSessionActivity = millis();
  return true;
}

void noCache() {
  http.sendHeader("Cache-Control", "no-store");
  http.sendHeader("X-Content-Type-Options", "nosniff");
  http.sendHeader("X-Frame-Options", "DENY");
  http.sendHeader("Referrer-Policy", "no-referrer");
}

void sendJson(int code, const JsonDocument &doc) {
  String output;
  serializeJson(doc, output);
  noCache();
  http.send(code, "application/json", output);
}

void sendMessage(int code, const char *message) {
  JsonDocument doc;
  doc["ok"] = code >= 200 && code < 300;
  doc["message"] = message;
  sendJson(code, doc);
}

bool requireAuth() {
  if (authenticated()) return true;
  sendMessage(401, "Sign in to manage this device");
  return false;
}

bool requireWriteAuth() {
  if (!requireAuth()) return false;
  if (csrfToken.isEmpty() || !constantTimeEqual(http.header("X-CSRF-Token"), csrfToken)) {
    sendMessage(403, "Missing or invalid CSRF token");
    return false;
  }
  return true;
}

bool readJson(JsonDocument &document) {
  if (http.arg("plain").length() > 2048) { sendMessage(413, "Request too large"); return false; }
  if (deserializeJson(document, http.arg("plain"))) {
    sendMessage(400, "Invalid JSON payload");
    return false;
  }
  return true;
}

void loginHandler() {
  if (static_cast<int32_t>(millis() - loginBlockedUntil) < 0) {
    sendMessage(429, "Too many failed attempts; try again shortly");
    return;
  }
  JsonDocument data;
  if (!readJson(data)) return;
  const String supplied = data["password"].as<String>();
  const String derived = sha256Hex(adminSalt + ":" + supplied);
  if (!constantTimeEqual(derived, adminHash)) {
    ++badLogins;
    if (badLogins >= 5) { loginBlockedUntil = millis() + 60000; badLogins = 0; }
    addLog("Rejected management login");
    sendMessage(401, "Incorrect password");
    return;
  }
  badLogins = 0;
  loginBlockedUntil = 0;
  sessionToken = randomString(40);
  csrfToken = randomString(40);
  lastSessionActivity = millis();
  http.sendHeader("Set-Cookie", "FLSESSION=" + sessionToken + "; Path=/; HttpOnly; SameSite=Strict");
  JsonDocument response;
  response["ok"] = true;
  response["csrf"] = csrfToken;
  sendJson(200, response);
  addLog("Management login successful");
}

void sessionHandler() {
  JsonDocument doc;
  doc["authenticated"] = authenticated();
  if (doc["authenticated"].as<bool>()) doc["csrf"] = csrfToken;
  sendJson(200, doc);
}

void logoutHandler() {
  if (!requireWriteAuth()) return;
  invalidateSession();
  http.sendHeader("Set-Cookie", "FLSESSION=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  sendMessage(200, "Signed out");
}

void statusHandler() {
  if (!requireAuth()) return;
  JsonDocument doc;
  doc["version"] = kVersion;
  doc["hostname"] = "freezy-fieldlight.local";
  doc["uptimeSec"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
  doc["ssid"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : config.ssid;
  doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["apActive"] = apRunning;
  doc["apSsid"] = apRunning ? apName() : "";
  doc["arenaConnected"] = freshArenaStatus();
  doc["arenaSocket"] = wsConnected;
  doc["arenaHost"] = config.arenaHost;
  doc["arenaPort"] = config.arenaPort;
  doc["wsConnects"] = wsConnects;
  doc["wsDrops"] = wsDrops;
  doc["arenaStatusFrames"] = wsStatusReceivedCount;
  doc["wsIgnoredMessages"] = wsIgnoredMessages;
  doc["wsStatusParseErrors"] = wsStatusParseErrors;
  doc["wsPeakStatusParseMs"] = wsPeakStatusParseMs;
  doc["fragmentedMessages"] = wsFragmentedCount;
  doc["fragmentErrors"] = wsFragmentErrorCount;
  doc["arenaWsPath"] = wsPath;
  doc["frameAgeSec"] = wsConnected ? static_cast<int>((millis() - lastFrameMs) / 1000) : -1;
  doc["monitorModeName"] = "Any team E-stop / A-stop";
  doc["arenaStatusFresh"] = freshArenaStatus();
  doc["teamsAssigned"] = freshArenaStatus() ? teamsAssigned : 0;
  JsonArray eStopped = doc["eStoppedStations"].to<JsonArray>();
  JsonArray aStopped = doc["aStoppedStations"].to<JsonArray>();
  if (freshArenaStatus()) {
    for (size_t i = 0; i < 6; ++i) {
      if (eStopMask & (1U << i)) eStopped.add(kStations[i]);
      if (aStopMask & (1U << i)) aStopped.add(kStations[i]);
    }
  }
  doc["matchState"] = freshField() ? fieldState : -1;
  doc["matchName"] = freshField() ? fieldStateName(fieldState) : "Unknown";
  const auto phase = fieldPhase();
  doc["phase"] = phase == field_light::FieldPhase::Idle ? "idle" :
                 phase == field_light::FieldPhase::Running ? "running" : "unknown";
  doc["manual"] = manualActive;
  doc["manualRemaining"] = manualActive ? (static_cast<uint32_t>(manualUntilMs - millis()) / 1000) : 0;
  doc["red"] = redOn;
  doc["green"] = greenOn;
  doc["relay1On"] = redOn;
  doc["relay2On"] = greenOn;
  doc["heartbeatPin"] = config.heartbeatPin;
  doc["runningPin"] = config.runningPin;
  sendJson(200, doc);
}

void configHandler() {
  if (!requireAuth()) return;
  JsonDocument doc;
  doc["ssid"] = config.ssid;
  doc["arenaHost"] = config.arenaHost;
  doc["arenaPort"] = config.arenaPort;
  doc["heartbeatPin"] = config.heartbeatPin;
  doc["runningPin"] = config.runningPin;
  doc["linkType"] = "2x independent active-HIGH 5V relay modules";
  doc["wifiPasswordSet"] = config.wifiPassword.length() > 0;
  sendJson(200, doc);
}

void saveConfigHandler() {
  if (!requireWriteAuth()) return;
  JsonDocument data;
  if (!readJson(data)) return;
  const String ssid = data["ssid"].as<String>();
  const String host = data["arenaHost"].as<String>();
  const int port = data["arenaPort"].as<int>();
  const int hbPin = data["heartbeatPin"].as<int>();
  const int runPin = data["runningPin"].as<int>();
  if (ssid.length() > 32 || (ssid.length() == 0 && !config.ssid.isEmpty())) {
    sendMessage(400, "Wi-Fi SSID must be 1-32 characters"); return;
  }
  if (!host.isEmpty() && !validHost(host)) { sendMessage(400, "Invalid field IP / hostname"); return; }
  if (port < 1 || port > 65535) { sendMessage(400, "Arena port must be between 1 and 65535"); return; }
  if (!validPin(hbPin) || !validPin(runPin) || hbPin == runPin) {
    sendMessage(400, "Choose distinct safe output pins (GPIO33 reserved)"); return;
  }
  String pass = data["wifiPassword"].as<String>();
  if (pass.length() > 63) { sendMessage(400, "Wi-Fi password too long"); return; }
  if (!ssid.isEmpty() && !pass.isEmpty() && pass.length() < 8) {
    sendMessage(400, "Wi-Fi password must be at least 8 characters"); return;
  }
  if (ssid != config.ssid && pass.isEmpty()) {
    sendMessage(400, "Enter a Wi-Fi password when changing networks"); return;
  }
  if (ssid != config.ssid) prefs.putString("ssid", ssid);
  if (!pass.isEmpty()) prefs.putString("wifiPass", pass);
  prefs.putString("host", host);
  prefs.putUShort("port", static_cast<uint16_t>(port));
  prefs.putUChar("hbPin", static_cast<uint8_t>(hbPin));
  prefs.putUChar("runPin", static_cast<uint8_t>(runPin));
  stopManualTest("Saved settings: stopping any manual test");
  setRelays(false, false);
  addLog("Settings saved; reboot scheduled");
  sendMessage(200, "Settings saved; device rebooting");
  rebootAtMs = millis() + 1300;
}

void manualTestHandler() {
  if (!requireWriteAuth()) return;
  JsonDocument data;
  if (!readJson(data)) return;
  const String mode = data["mode"].as<String>();
  if (mode == "auto") {
    stopManualTest("Manual test cancelled; automatic control restored");
    updateOutputs();
    sendMessage(200, "Automatic control restored");
    return;
  }
  if (mode != "red" && mode != "green" && mode != "both" && mode != "off") {
    sendMessage(400, "Choose red, green, both, off, or auto"); return;
  }
  if (fieldPhase() == field_light::FieldPhase::Running) {
    sendMessage(409, "Testing disabled while an actual match runs"); return;
  }
  manualCommand = {mode == "red" || mode == "both",
                   mode == "green" || mode == "both"};
  manualActive = true;
  manualUntilMs = millis() + kTestDurationMs;
  updateOutputs();
  addLog("Direct relay test active for 15 seconds: " + mode);
  sendMessage(200, "Relay test active; returns to automatic in 15 seconds");
}

void logHandler() {
  if (!requireAuth()) return;
  JsonDocument data;
  JsonArray lines = data["events"].to<JsonArray>();
  const size_t start = (eventHead + kLogCount - eventUsed) % kLogCount;
  for (size_t i = 0; i < eventUsed; ++i) {
    const EventLine &line = events[(start + i) % kLogCount];
    JsonObject item = lines.add<JsonObject>();
    item["seconds"] = line.atMs / 1000;
    item["message"] = line.text;
  }
  sendJson(200, data);
}

void changePasswordHandler() {
  if (!requireWriteAuth()) return;
  JsonDocument data;
  if (!readJson(data)) return;
  const String oldPass = data["oldPassword"].as<String>();
  const String newPass = data["newPassword"].as<String>();
  if (!constantTimeEqual(sha256Hex(adminSalt + ":" + oldPass), adminHash)) {
    sendMessage(403, "Current password incorrect"); return;
  }
  if (newPass.length() < 12 || newPass.length() > 64) {
    sendMessage(400, "New password must be 12-64 characters"); return;
  }
  adminSalt = randomString(22);
  adminHash = sha256Hex(adminSalt + ":" + newPass);
  prefs.putString("salt", adminSalt);
  prefs.putString("pwdHash", adminHash);
  invalidateSession();
  addLog("Management password changed; sessions invalidated");
  http.sendHeader("Set-Cookie", "FLSESSION=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  sendMessage(200, "Password updated. Sign in again.");
}

void apHandler() {
  if (!requireWriteAuth()) return;
  apExplicitUntil = millis() + 10UL * 60UL * 1000UL;
  startSetupAp();
  sendMessage(200, "Setup AP enabled for 10 minutes on connected Wi-Fi. Password is printed in Serial Monitor.");
}

void rebootHandler() {
  if (!requireWriteAuth()) return;
  stopManualTest("Reboot requested");
  setRelays(false, false);
  sendMessage(200, "Rebooting");
  rebootAtMs = millis() + 750;
}

bool otaRequestAuthorized() {
  // Check *before* flash writes, including chunk callbacks; fail closed.
  return authenticated() && !csrfToken.isEmpty() &&
         constantTimeEqual(http.header("X-CSRF-Token"), csrfToken);
}

void otaUpload() {
  if (!otaRequestAuthorized()) return;
  HTTPUpload &upload = http.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaStarted = true;
    otaSucceeded = false;
    stopManualTest("Firmware update started");
    setRelays(false, false);
    addLog("OTA firmware upload started");
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      addLog("OTA initialization failed");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaStarted && !Update.hasError() && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      addLog("OTA flash write failed");
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (otaStarted && !Update.hasError() && Update.end(true)) {
      otaSucceeded = true;
      addLog("OTA image verified and written; restarting");    } else addLog("OTA image verification / write failed");
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaStarted = false;
    addLog("OTA transfer aborted");
  }
}

void otaFinish() {
  if (!requireWriteAuth()) return;
  if (!otaStarted || !otaSucceeded) {
    sendMessage(400, "Update failed; upload a valid ESP32 firmware.bin");
    otaStarted = false;
    return;
  }
  sendMessage(200, "Firmware installed. Rebooting now.");
  rebootAtMs = millis() + 1100;
}

void serveUi() {
  noCache();
  http.send_P(200, "text/html; charset=utf-8", kWebHtml);
}

void setupHttp() {
  const char *headNames[] = {"Cookie", "X-CSRF-Token"};
  http.collectHeaders(headNames, 2);
  http.on("/", HTTP_GET, serveUi);
  http.on("/api/session", HTTP_GET, sessionHandler);
  http.on("/api/login", HTTP_POST, loginHandler);
  http.on("/api/logout", HTTP_POST, logoutHandler);
  http.on("/api/status", HTTP_GET, statusHandler);
  http.on("/api/config", HTTP_GET, configHandler);
  http.on("/api/config", HTTP_POST, saveConfigHandler);
  http.on("/api/test", HTTP_POST, manualTestHandler);
  http.on("/api/logs", HTTP_GET, logHandler);
  http.on("/api/password", HTTP_POST, changePasswordHandler);
  http.on("/api/setup-ap", HTTP_POST, apHandler);
  http.on("/api/reboot", HTTP_POST, rebootHandler);
  http.on("/api/ota", HTTP_POST, otaFinish, otaUpload);
  http.onNotFound([]() { sendMessage(404, "Not found"); });
  http.begin();
  addLog("Management HTTP server listening on port 80");
}

void checkRecoveryButton() {
  if (digitalRead(kButtonPin) == LOW) {
    if (!buttonDownSince) buttonDownSince = millis();
    if (!resetButtonActivated && static_cast<uint32_t>(millis() - buttonDownSince) >= kButtonHoldMs) {
      resetButtonActivated = true;
      resetCredentials();
    }
  } else {
    buttonDownSince = 0;
    resetButtonActivated = false;
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.printf("\nFreezy Arena Field Light %s - ESP32\n", kVersion);
  prefs.begin("field-light", false);
  loadConfig();
  initializeRelayPins();
  pinMode(kButtonPin, INPUT_PULLUP);
  ensureCredentials();
  addLog("Boot: 2 relays LOW; RED GPIO26=any team E-stop, GREEN GPIO27=any team A-stop");
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  setupHttp();
  if (config.ssid.isEmpty()) startSetupAp();
  else connectWifi();
}

void loop() {
  checkRecoveryButton();
  manageWifi();
  runArenaSocket();
  updateOutputs();
  http.handleClient();
  if (rebootAtMs && static_cast<int32_t>(millis() - rebootAtMs) >= 0) {
    setRelays(false, false);
    delay(40);
    ESP.restart();
  }
  delay(2);
}