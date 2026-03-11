## Detailed File Plan

### hevel

`src/hevel.h`

- add portal session structs
- add EIS state structs
- add zone/barrier structs

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

`src/inputcapture.c`

- InputCapture session lifecycle
- zones
- barriers
- release handling

`src/eis.c`

- libeis event loop integration
- device advertisement
- event dispatch to SWC wrappers

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
