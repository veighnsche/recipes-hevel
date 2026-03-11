## Detailed File Plan

### hevel

`src/hevel.h`

- add portal session structs
- add EIS state structs
- add zone/barrier structs
- post-`R1` ownership target:
  - keep compositor-global state
  - keep top-level init/finalize entrypoints
  - keep only declarations that cross subsystem boundaries
  - move subsystem-private declarations into internal headers
- `R2` landed:
  - InputCapture-private declarations now live in `src/inputcapture.h`

`src/hevel.c`

- initialize portal backend
- initialize EIS
- register D-Bus service
- integrate cleanup with compositor shutdown

`src/window.c`

- trigger zone invalidation on screen add/remove

`src/input.c`

- only minor integration if portal mode needs WM-visible state
- most work should stay out of this file

`src/portal.c`

- D-Bus bootstrap
- object registration
- request/session bookkeeping shared by both portal interfaces

`src/remotedesktop.c`

- RemoteDesktop session lifecycle
- approval path
- EIS fd handoff
- target split after the pre-`M2P3` refactor:
  - `src/remotedesktop-session.c`
  - `src/remotedesktop-approval.c`
  - `src/remotedesktop-methods.c`
  - `src/remotedesktop-signals.c`
  - `src/remotedesktop.h`

`src/remotedesktop.h`

- internal RemoteDesktop-only declarations
- request/session structs
- pending approval state
- session-state enums
- helpers shared only by split RemoteDesktop modules

`src/inputcapture.c`

- InputCapture session lifecycle
- zones
- barriers
- release handling
- `R2` landed:
  - thin aggregator and vtable layer only
  - includes:
    - `src/inputcapture-session.c`
    - `src/inputcapture-zones.c`
    - `src/inputcapture-barriers.c`
    - `src/inputcapture-methods.c`
    - `src/inputcapture-signals.c`
    - `src/inputcapture.h`

`src/inputcapture.h`

- internal InputCapture-only declarations
- request/session structs
- zone and barrier structs
- InputCapture-local enums
- helpers shared only by split InputCapture modules

`src/eis.c`

- libeis event loop integration
- device advertisement
- event dispatch to SWC wrappers
- target split after the pre-`M2P3` refactor:
  - `src/eis-core.c`
  - `src/eis-devices.c`
  - `src/eis-forward.c`
  - `src/eis.h`

`src/eis.h`

- internal EIS-only declarations
- request and client structs
- capability and device helpers
- helpers shared only by split EIS modules

`src/inject.c`

- compositor bridge transport
- peer authorization
- spawn bridge
- local CLI and EIS CLI helpers
- target split after the pre-`M2P3` refactor:
  - `src/inject-transport.c`
  - `src/inject-auth.c`
  - `src/inject-spawn.c`
  - `src/inject-zones.c`
  - `src/inject-cli.c`
  - `src/eis-cli.c`
  - `src/inject.h`

`src/inject.h`

- internal inject-only declarations
- bridge transport helpers
- peer authorization helpers
- spawn bridge helpers
- zone bridge helpers
- CLI entrypoints shared across split inject modules

### neuswc

`libswc/swc.h`

- public injection API
- public capture/barrier control API

`libswc/keyboard.c`

- wrapper for external key injection
- cleanup/stuck-key handling

`libswc/pointer.c`

- external pointer injection wrappers
- barrier crossing logic
- capture-active pointer behavior

`libswc/pointer.h`

- barrier structs
- hook declarations

`libswc/swc.c`

- glue between compositor-global state and pointer capture region/barriers

## Testing Plan
