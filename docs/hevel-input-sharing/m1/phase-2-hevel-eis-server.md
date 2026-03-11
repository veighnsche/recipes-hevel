### Phase 2: Add EIS server support to hevel

Status: done on March 10, 2026.

Objective:

- make `hevel` accept EIS sender connections and turn them into compositor
  input events

#### Stage 2.1: Add persistent EIS state to hevel

Units of Work:

- add state for the root `struct eis *` context
- add state for the active EIS event-loop integration
- add state for connected EIS clients
- add state for the exported logical seat
- add state for the exported logical keyboard device
- add state for the exported logical pointer device
- add session-to-client mapping state

Target files:

- `src/hevel.h`
- `src/eis.c`
- `src/hevel.c`

#### Stage 2.2: Accept sender connections and advertise devices

Units of Work:

- create the libeis context
- add a reusable fd handoff helper for future portal sessions
- add a local debug fd handoff path through the existing control socket
- accept a client connection for a queued EIS request
- bind the accepted client to the matching queued request state
- advertise the logical seat for the active Wayland session
- advertise the logical keyboard device
- advertise the logical relative and absolute pointer devices
- constrain exposed capabilities to the queued capability mask

Target files:

- `src/eis.c`
- `src/inject.c`

#### Stage 2.3: Dispatch incoming EIS events into SWC

Units of Work:

- register the EIS fd with the existing Wayland event loop
- process pending EIS events from the event-loop callback
- dispatch EIS key events to `swc_keyboard_inject_key()`
- dispatch EIS relative-motion events to
  `swc_pointer_inject_relative_motion()`
- dispatch EIS absolute-motion events to
  `swc_pointer_inject_absolute_motion()`
- dispatch EIS button events to `swc_pointer_inject_button()`
- dispatch EIS axis events to `swc_pointer_inject_axis()`
- dispatch EIS frame events to `swc_pointer_inject_frame()`
- tear down client-linked EIS state on disconnect
- tear down session-linked EIS state on session close

Target files:

- `src/eis.c`
- `src/hevel.c`
- `src/inject.c`

Acceptance criteria:

- `hevel` can accept an EIS sender connection and log incoming events
- injected EIS events reach the SWC wrapper layer

Result:

- `hevel` now starts a compositor-local libeis server during normal startup
- queued EIS clients are accepted and exported with seat/device state
- incoming EIS keyboard, pointer, button, scroll, and frame events are
  forwarded into the Phase 1 SWC injection wrappers
- `hevel --eis ...` now provides a libei sender-based debug client for
  exercising the Phase 2 path without pretending the RemoteDesktop portal
  lifecycle already exists

Live verification on March 10, 2026:

- a live `hevel` session accepted a debug EIS sender connection from
  `hevel --eis`
- `hevel --eis ping` returned `ok`
- `hevel --eis motion 100 0` returned `sent`
- tracing the running compositor confirmed:
  - the EIS client was accepted
  - a motion event was received
  - a frame event was received
  - the client was then disconnected cleanly

Runtime note:

- after installing a new `hevel` build, the session must be restarted before
  the compositor exposes the updated local EIS/control-socket behavior
- the portal backend still does not hand out approved EIS fds; that remains
  Phase 3 work

