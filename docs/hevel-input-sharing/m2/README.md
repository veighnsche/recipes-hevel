## Milestone 2: Seamless pointer/keyboard handoff via InputCapture

Status: in progress on March 10, 2026.

Milestone completion criteria:

- an InputCapture client can create a session against the `hevel` backend
- `GetZones` returns the current `hevel` output layout
- pointer barriers can be installed for the current zone set
- crossing an enabled barrier activates capture instead of clipping locally
- while capture is active, pointer and keyboard events are routed to the
  approved EIS client
- `Release` returns control locally and applies the release cursor hint

Refactor gate before `M2P3`:

- [Intermezzo: Hevel Boundary](intermezzo-hevel-boundary.md)
- [Intermezzo Companion: Large-file Refactor](intermezzo-large-file-refactor.md)
