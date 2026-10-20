# Add Compact Field Light Status to Freezy Arena

Run `python3 apply_compact_status.py` from inside the **Freezy Arena repository root** only after copying the script there, or invoke it from any path while your shell working directory is the repo root. It edits three existing Go files locally but does not commit or push. Before deploying, run `gofmt -w field/arena_notifiers.go field/arena.go web/api.go`, `go test ./...` (or your standard repository test commands), and inspect `git diff`.

This adds `fieldLightStatus` to `/api/arena/websocket` (a public read-only endpoint) and sends it on the existing ~500ms field status interval. Its JSON data is `{MatchState,CanStartMatch,RobotsAssigned,RobotsSatisfied,RobotsReady}`. Modes 2/3 of the ESP32 v1.5 depend on this message. Mode 1 continues to use existing `matchTime`.

Robot 'ready' here is `(RobotLinked or Bypass) AND not EStop` for each **assigned** team; 0 teams is not treated ready. `CanStartMatch` preserves native FMS readiness checks, including field E-stop, FTA Ready, a-stop reset, and PLC state when the FMS PLC is enabled.

No changes to the FMS match-start controls, driver-station outputs, browser displays, or Horner PLC are made. Because the source files in your repository may have changed since the referenced main snapshot, the patcher will refuse to apply if its exact anchor text differs.