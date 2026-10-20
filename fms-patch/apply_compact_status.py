#!/usr/bin/env python3
"""Add a compact fieldLightStatus notifier on Freezy Arena's public arena WebSocket.
Run from repository root of Freezy-Arena/freezy-arena. Does not commit anything.
Refuses to run if expected anchors do not match. Idempotent on already-patched tree.
"""
from pathlib import Path


def patch(path, replacements):
    file = Path(path)
    if not file.exists():
        raise SystemExit(f"Missing {path}; run script from Freezy Arena repo root")
    content = file.read_text(encoding="utf-8")
    if "FieldLightStatusNotifier" in content:
        print(f"Already patched: {path}")
        return
    for old, new in replacements:
        if content.count(old) != 1:
            raise SystemExit(f"Unexpected upstream file layout: {path}; expected exactly one anchor: {old[:90]!r}")
        content = content.replace(old, new)
    file.write_text(content, encoding="utf-8")
    print(f"Updated {path}")


def main():
    patch("field/arena_notifiers.go", [
        ("\tArenaStatusNotifier                *websocket.Notifier\n",
         "\tArenaStatusNotifier                *websocket.Notifier\n"
         "\tFieldLightStatusNotifier           *websocket.Notifier\n"),
        ('\tarena.ArenaStatusNotifier = websocket.NewNotifier("arenaStatus", arena.generateArenaStatusMessage)\n',
         '\tarena.ArenaStatusNotifier = websocket.NewNotifier("arenaStatus", arena.generateArenaStatusMessage)\n'
         '\tarena.FieldLightStatusNotifier = websocket.NewNotifier("fieldLightStatus", arena.generateFieldLightStatusMessage)\n'),
        ("func (arena *Arena) generateArenaStatusMessage() any {",
         """// Compact, read-only status for low-bandwidth field indicator clients.
// This supplements (and does not change) the existing arenaStatus payload.
func (arena *Arena) generateFieldLightStatusMessage() any {
    assigned, satisfied := 0, 0
    for _, name := range []string{"R1", "R2", "R3", "B1", "B2", "B3"} {
        station := arena.AllianceStations[name]
        if station == nil || station.Team == nil || station.Team.Id <= 0 {
            continue
        }
        assigned++
        linked := station.DsConn != nil && station.DsConn.RobotLinked
        if (linked || station.Bypass) && !station.EStop {
            satisfied++
        }
    }
    return struct {
        MatchState      MatchState
        CanStartMatch   bool
        RobotsAssigned  int
        RobotsSatisfied int
        RobotsReady     bool
    }{
        arena.MatchState,
        len(arena.getStartMatchConditions()) == 0,
        assigned,
        satisfied,
        assigned > 0 && assigned == satisfied,
    }
}

func (arena *Arena) generateArenaStatusMessage() any {"""),
    ])
    patch("field/arena.go", [
        ('\t\tarena.ArenaStatusNotifier.Notify()\n',
         '\t\tarena.ArenaStatusNotifier.Notify()\n'
         '\t\tarena.FieldLightStatusNotifier.Notify()\n'),
    ])
    patch("web/api.go", [
        ('ws.HandleNotifiers(web.arena.MatchTimingNotifier, web.arena.MatchLoadNotifier, web.arena.MatchTimeNotifier)',
         'ws.HandleNotifiers(web.arena.MatchTimingNotifier, web.arena.MatchLoadNotifier, '
         'web.arena.MatchTimeNotifier, web.arena.FieldLightStatusNotifier)'),
    ])
    print("\nNext: gofmt -w field/arena_notifiers.go field/arena.go web/api.go")
    print("Then run go test ./...; inspect git diff; deploy after testing.")

if __name__ == "__main__":
    main()