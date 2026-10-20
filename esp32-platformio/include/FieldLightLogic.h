#pragma once
#include <stdint.h>

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
}  // namespace field_light