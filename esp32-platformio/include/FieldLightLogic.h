#pragma once
#include <stdint.h>
#include <stddef.h>

// Read-only Freezy Arena match status interpretation for Horner PLC analog inputs.
// Freezy-Arena/freezy-arena main protocol @ 49d5ceae2d72526bd7414bdb1b1fa45145221e34
namespace field_light {
enum MatchState : int {
  PreMatch = 0,
  StartMatch = 1,
  AutoPeriod = 2,
  PausePeriod = 3,
  TeleopPeriod = 4,
  PostMatch = 5,
  TimeoutActive = 6,
  PostTimeout = 7,
};
enum class FieldPhase : uint8_t { Unknown, Idle, Running };

inline FieldPhase phaseForState(int state, bool fresh) {
  if (!fresh) return FieldPhase::Unknown;
  switch (state) {
    case StartMatch:
    case AutoPeriod:
    case PausePeriod:
    case TeleopPeriod: return FieldPhase::Running;
    case PreMatch:
    case PostMatch:
    case TimeoutActive:
    case PostTimeout: return FieldPhase::Idle;
    default: return FieldPhase::Unknown;
  }
}

enum class MonitorMode : uint8_t { MatchRunning = 1, RobotConnectivity = 2, FieldReadiness = 3 };

inline const char *monitorModeName(MonitorMode mode) {
  switch (mode) {
    case MonitorMode::MatchRunning: return "Match running";
    case MonitorMode::RobotConnectivity: return "Robot connectivity";
    case MonitorMode::FieldReadiness: return "Field readiness";
    default: return "Unknown";
  }
}


// Classify the first WebSocket frame without JSON parsing. Freezy Arena's
// websocket.Message struct writes `type` before `data`, so a small prefix
// contains the routing information even for multi-frame payloads.
enum class WsMessageKind : uint8_t { Ignore, ArenaStatus, MatchTime, Ping, OtherHeartbeat };

inline WsMessageKind classifyWsMessage(const uint8_t *p, size_t size) {
  if (!p || size < 9) return WsMessageKind::Ignore;
  const size_t limit = size < 128 ? size : 128;
  for (size_t i = 0; i + 6 < limit; ++i) {
    if (p[i] != '"' || p[i+1] != 't' || p[i+2] != 'y' ||
        p[i+3] != 'p' || p[i+4] != 'e' || p[i+5] != '"') continue;
    size_t j = i + 6;
    while (j < limit && (p[j] == ' ' || p[j] == '\t')) ++j;
    if (j >= limit || p[j++] != ':') continue;
    while (j < limit && (p[j] == ' ' || p[j] == '\t')) ++j;
    if (j >= limit || p[j++] != '"') continue;
    const size_t begin = j;
    while (j < limit && p[j] != '"') ++j;
    if (j == limit) return WsMessageKind::Ignore;
    const size_t n = j - begin;
    auto same = [&](const char *value, size_t valueLength) {
      if (n != valueLength) return false;
      for (size_t k = 0; k < n; ++k) if (p[begin+k] != static_cast<uint8_t>(value[k])) return false;
      return true;
    };
    if (same("arenaStatus", 11)) return WsMessageKind::ArenaStatus;
    if (same("matchTime", 9)) return WsMessageKind::MatchTime;
    if (same("ping", 4)) return WsMessageKind::Ping;
    if (same("matchTiming", 11) || same("matchLoad", 9)) return WsMessageKind::OtherHeartbeat;
    return WsMessageKind::Ignore;
  }
  return WsMessageKind::Ignore;
}

struct PlcSignalRequest {
  bool heartbeatEnabled;
  bool runningHigh;
};
inline PlcSignalRequest signalsForPhase(FieldPhase phase) {
  if (phase == FieldPhase::Running) return {true, true};
  if (phase == FieldPhase::Idle) return {true, false};
  return {false, false};
}
// A fresh FMS match state permits RED when status is not yet available.
// Readiness can only show GREEN for CanStartMatch when arenaStatus is fresh.
inline PlcSignalRequest signalsForMode(MonitorMode mode, FieldPhase phase, int matchState,
                                       bool statusFresh, bool robotsReady, bool canStart) {
  if (phase == FieldPhase::Unknown) return {false, false};
  if (mode == MonitorMode::MatchRunning) return signalsForPhase(phase);
  if (mode == MonitorMode::RobotConnectivity)
    return {true, statusFresh && robotsReady};
  if (mode == MonitorMode::FieldReadiness)
    return {true, phase == FieldPhase::Running ||
                    (statusFresh && matchState == PreMatch && canStart)};
  return {false, false};
}
// Two independent relays. Missing/stale status is a fault even if the
// WebSocket TCP connection appears to be open.
struct DualRelayRequest { bool robotFault; bool fmsConnected; bool flashFmsRelay; };
inline DualRelayRequest dualRelayRequest(bool freshStatus, bool robotsReady) {
  if (!freshStatus) return {true, false, true};
  return {!robotsReady, true, false};
}
inline bool healthRelayOutput(DualRelayRequest requested, uint32_t now,
                              uint32_t blinkHalfCycleMs = 1000) {
  if (!requested.flashFmsRelay) return requested.fmsConnected;
  return blinkHalfCycleMs != 0 && (now / blinkHalfCycleMs) % 2U == 0;
}

// Each Freezy Arena alliance station has independent EStop and AStop flags.
// Unassigned stations cannot represent a team stop, so ignore those.
// No inferred stop or flashing if the feed is unavailable; status is UNKNOWN.
struct StationStopState {
  bool assigned;
  bool eStopped;
  bool aStopped;
};
struct StopRelayRequest {
  bool redEStop;
  bool greenAStop;
};
inline StopRelayRequest stopRelaysForStations(bool statusFresh,
    const StationStopState *stations, size_t count) {
  StopRelayRequest request{false, false};
  if (!statusFresh || !stations) return request;
  for (size_t i = 0; i < count; ++i) {
    if (!stations[i].assigned) continue;
    request.redEStop = request.redEStop || stations[i].eStopped;
    request.greenAStop = request.greenAStop || stations[i].aStopped;
  }
  return request;
}
}  // namespace field_light