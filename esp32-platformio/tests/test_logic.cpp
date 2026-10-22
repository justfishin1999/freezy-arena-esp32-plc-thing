#include "../include/FieldLightLogic.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
using namespace field_light;
int main() {
  for (int state : {0, 5, 6, 7}) {
    assert(phaseForState(state, true) == FieldPhase::Idle);
    auto outputs = signalsForPhase(phaseForState(state, true));
    assert(outputs.heartbeatEnabled && !outputs.runningHigh);
  }
  for (int state : {1, 2, 3, 4}) {
    assert(phaseForState(state, true) == FieldPhase::Running);
    auto outputs = signalsForPhase(phaseForState(state, true));
    assert(outputs.heartbeatEnabled && outputs.runningHigh);
  }
  for (int state=-5;state<12;state++) {
    auto outputs=signalsForPhase(phaseForState(state,false));
    assert(!outputs.heartbeatEnabled && !outputs.runningHigh);
  }
  for (int state : {-1,8,9,99}) {
    auto outputs=signalsForPhase(phaseForState(state,true));
    assert(!outputs.heartbeatEnabled && !outputs.runningHigh);
  }
  // In every mode stale link/status fails OFF, never green.
  for (MonitorMode mode : {MonitorMode::MatchRunning, MonitorMode::RobotConnectivity, MonitorMode::FieldReadiness}) {
    auto fail = signalsForMode(mode, FieldPhase::Unknown, PreMatch, true, true, true);
    assert(!fail.heartbeatEnabled && !fail.runningHigh);
  }
  for (auto state : {PreMatch, PostMatch, TimeoutActive}) {
    const auto req = signalsForMode(MonitorMode::MatchRunning, FieldPhase::Idle, state, false, true, true);
    assert(req.heartbeatEnabled && !req.runningHigh);
  }
  auto connected = signalsForMode(MonitorMode::RobotConnectivity, FieldPhase::Idle, PreMatch, true, true, false);
  assert(connected.heartbeatEnabled && connected.runningHigh);
  auto disconnected = signalsForMode(MonitorMode::RobotConnectivity, FieldPhase::Idle, PreMatch, true, false, true);
  assert(disconnected.heartbeatEnabled && !disconnected.runningHigh);
  auto noData = signalsForMode(MonitorMode::RobotConnectivity, FieldPhase::Idle, PreMatch, false, true, true);
  assert(noData.heartbeatEnabled && !noData.runningHigh); // FMS online: signal RED if readiness unknown
  auto ready = signalsForMode(MonitorMode::FieldReadiness, FieldPhase::Idle, PreMatch, true, false, true);
  assert(ready.heartbeatEnabled && ready.runningHigh);
  auto notReady = signalsForMode(MonitorMode::FieldReadiness, FieldPhase::Idle, PreMatch, true, true, false);
  assert(notReady.heartbeatEnabled && !notReady.runningHigh);
  auto inMatch = signalsForMode(MonitorMode::FieldReadiness, FieldPhase::Running, TeleopPeriod, true, false, false);
  assert(inMatch.heartbeatEnabled && inMatch.runningHigh);
  auto stillRunningWithStaleStatus = signalsForMode(MonitorMode::FieldReadiness, FieldPhase::Running, TeleopPeriod, false, false, false);
  assert(stillRunningWithStaleStatus.heartbeatEnabled && stillRunningWithStaleStatus.runningHigh);
  auto stalePreMatch = signalsForMode(MonitorMode::FieldReadiness, FieldPhase::Idle, PreMatch, false, false, true);
  assert(stalePreMatch.heartbeatEnabled && !stalePreMatch.runningHigh);
  auto post = signalsForMode(MonitorMode::FieldReadiness, FieldPhase::Idle, PostMatch, true, true, true);
  assert(post.heartbeatEnabled && !post.runningHigh);
  uint32_t nearWrap = 0xFFFFFF00U;
  uint32_t afterWrap = 800U;
  assert(uint32_t(afterWrap - nearWrap) == 1056U);
  // No assigned teams and no stops -> no lights.
  StationStopState stations[6] = {
    {true, false, false}, {true, false, false}, {true, false, false},
    {true, false, false}, {true, false, false}, {false, true, true}
  };
  auto outputs = stopRelaysForStations(true, stations, 6);
  assert(!outputs.redEStop && !outputs.greenAStop); // ignore unassigned station
  stations[1].eStopped = true;
  outputs = stopRelaysForStations(true, stations, 6);
  assert(outputs.redEStop && !outputs.greenAStop);
  stations[4].aStopped = true;
  outputs = stopRelaysForStations(true, stations, 6);
  assert(outputs.redEStop && outputs.greenAStop); // different teams can trigger both
  stations[1].eStopped = false;
  outputs = stopRelaysForStations(true, stations, 6);
  assert(!outputs.redEStop && outputs.greenAStop);
  stations[4].aStopped = false;
  stations[2].eStopped = true;
  stations[2].aStopped = true;
  outputs = stopRelaysForStations(true, stations, 6);
  assert(outputs.redEStop && outputs.greenAStop); // one team can trigger both
  outputs = stopRelaysForStations(false, stations, 6);
  assert(!outputs.redEStop && !outputs.greenAStop); // FMS lost -> unknown, neither asserted
  outputs = stopRelaysForStations(true, nullptr, 6);
  assert(!outputs.redEStop && !outputs.greenAStop);
  stations[2].assigned = false;
  outputs = stopRelaysForStations(true, stations, 6);
  assert(!outputs.redEStop && !outputs.greenAStop);

  // Stock Freezy Arena websocket envelope. Skip scores and other large
  // display data before buffering or deserializing (prevents slow listeners).
  auto identify = [](const char *data) {
    return classifyWsMessage(reinterpret_cast<const uint8_t *>(data),
                             std::strlen(data));
  };
  assert(identify("{\"type\":\"arenaStatus\",\"data\":{") == WsMessageKind::ArenaStatus);
  assert(identify("{\"type\":\"matchTime\",\"data\":{") == WsMessageKind::MatchTime);
  assert(identify("{\"type\":\"ping\",\"data\":null}") == WsMessageKind::Ping);
  assert(identify("{\"type\":\"matchTiming\",\"data\":{") == WsMessageKind::OtherHeartbeat);
  assert(identify("{\"type\":\"matchLoad\",\"data\":{") == WsMessageKind::OtherHeartbeat);
  assert(identify("{\"type\":\"realtimeScore\",\"data\":{") == WsMessageKind::Ignore);
  assert(identify("{\"type\":\"displayConfiguration\",\"data\":{") == WsMessageKind::Ignore);
  assert(identify("{\"data\":{},\"type\":\"arenaStatus\"}") == WsMessageKind::ArenaStatus);
  assert(identify("{\"type\":\"bogus\"}") == WsMessageKind::Ignore);
  assert(identify("{\"type\":\"arenaSta") == WsMessageKind::Ignore);
  assert(identify("") == WsMessageKind::Ignore);
  std::cout << "Freezy Arena ESP32 E-stop and A-stop mapping: PASS\n";
}