# Hevel Input Sharing Plan

Date: March 10, 2026

Status: Milestone 0 is complete enough to start Milestone 1.

## Goal

Add standards-based mouse/keyboard sharing to the `wayland.fyi` stack so a
`hevel` session can participate in software KVM workflows.

Two target modes matter:

1. `hevel` as the controlled machine
   - Another machine takes over this desktop and injects pointer/keyboard input.
   - This maps to the XDG RemoteDesktop portal plus EIS.
2. `hevel` as the machine that owns the local mouse/keyboard
   - Pointer/keyboard cross the edge of a `hevel` output layout and get handed
     to another machine.
   - This maps to the XDG InputCapture portal plus EIS.

The implementation should stay inside the `wayland.fyi` stack:

- `hevel`
- `neuswc`
- `neuwld`
- minimal portal/EIS dependencies only

No wlroots migration, no GNOME/KDE backend reuse, no compositor swap.

## Recommendation

Implement the portal backend in `hevel`, not in a separate helper process, for
the first iteration.

Reason:

- `hevel` already owns the session event loop.
- `hevel` already tracks screen topology and current screen.
- `neuswc` should continue owning the low-level input pipeline.
- The portal backend needs desktop-specific policy and screen semantics, which
  belong closer to `hevel` than to generic `neuswc`.

So the split should be:

- `hevel`: portal backend, session lifecycle, policy, zone/barrier management
- `neuswc`: low-level event injection, pointer barrier crossing, capture state

## Current State

### What already exists

- `hevel` tracks compositor state and screens in `src/hevel.h`, `src/hevel.c`,
  and `src/window.c`.
- `neuswc` already has internal entry points for keyboard and pointer handling:
  - `keyboard_handle_key()`
  - `pointer_handle_button()`
  - `pointer_handle_axis()`
  - `pointer_handle_relative_motion()`
  - `pointer_handle_absolute_motion()`
  - `pointer_handle_frame()`
- `neuswc` already exposes output and screen geometry.

### What is missing

- No functional portal session handling yet.
- No EIS fd handoff from portal sessions.
- No event injection path from `hevel` into `neuswc`.
- No pointer barrier activation/release logic.
- No permission or session UX for capture/control.
- No real `InputCapture` behavior yet.
- No real `RemoteDesktop` behavior yet.

### Important code observations

- `neuswc` currently clips pointer movement to its local region before
  dispatching motion:
  - `libswc/pointer.c`: `clip_position()`
  - `libswc/pointer.c`: `pointer_handle_absolute_motion()`
- `neuswc` reads physical input through libinput and dispatches directly into
  SWC input handlers:
  - `libswc/seat.c`
- `hevel` already owns:
  - the Wayland event loop
  - the linked list of screens
  - current-screen tracking

## Architecture

### Milestone 1 architecture: RemoteDesktop only

This is the smallest useful feature.

Flow:

1. `xdg-desktop-portal` calls the `hevel` backend over D-Bus.
2. `hevel` creates a RemoteDesktop session and hands back an EIS fd.
3. A remote-control client connects over EIS.
4. `hevel` receives EIS events and forwards them into `neuswc` injection
   wrappers.
5. `neuswc` injects them into the normal keyboard/pointer pipeline.

This makes `hevel` controllable by another machine.

### Milestone 2 architecture: InputCapture

This is the seamless edge-crossing feature.

Flow:

1. `hevel` exposes zones based on current outputs.
2. A client sets pointer barriers through the InputCapture portal.
3. `hevel` pushes those barriers down into `neuswc`.
4. `neuswc` detects crossing before pointer clipping, activates capture, and
   stops local cursor progression.
5. EIS delivers events to the remote client.
6. On release, `hevel` optionally repositions the local cursor using the portal
   `cursor_position` hint and resumes normal local input.

## Why not liboeffis

`liboeffis` is useful as a client-side wrapper for applications using the
RemoteDesktop portal. It is not the right core dependency for the compositor
backend.

Use it for:

- interoperability testing
- simple client-side harnesses

Do not use it as the implementation basis for the `hevel` portal backend.

## Implementation Backlog

## Milestone 0: Build and packaging prerequisites

Status: done on March 10, 2026.

What landed:

- `rocky-hevel-bootstrap.rhai` now installs the build/runtime prerequisites
  needed for the portal/EIS path:
  - `dbus-devel`
  - `libei`
  - `libeis`
  - `xdg-desktop-portal`
  - `meson`
  - `ninja-build`
  - `python3-jinja2`
  - `python3-attrs`
- `libei-local.rhai` now builds and installs `libei`/`libeis` 1.3.0 from
  source into `/usr/local`, including headers and `pkg-config` metadata, to
  cover Rocky 10.1's missing `-devel` package.
- `hevel.rhai` now rebuilds `hevel` from a recipe-managed source overlay that
  adds:
  - `src/portal.c`
  - `src/remotedesktop.c`
  - `src/inputcapture.c`
  - `src/eis.c`
  - `--portal-service` mode in `hevel`
- `hevel-portal-scaffold.rhai` now installs live backend discovery metadata
  into:
  - `/usr/share/xdg-desktop-portal/portals/hevel.portal`
  - `/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hevel.service`
  - `/usr/share/xdg-desktop-portal/hevel-portals.conf`
- Old failed metadata in `/usr/local/share` is removed as part of the scaffold
  recipe so discovery stays deterministic.

What the scaffold currently does:

- Exports `org.freedesktop.impl.portal.InputCapture` version `1`
- Exports `org.freedesktop.impl.portal.RemoteDesktop` version `2`
- Activates on the user session bus via `hevel --portal-service`
- Is discoverable by `xdg-desktop-portal` when
  `XDG_CURRENT_DESKTOP=hevel`

What the scaffold does not do yet:

- `CreateSession`, `GetZones`, `SetPointerBarriers`, `Enable`, `Disable`,
  `Release`, `SelectDevices`, and `Start` do not succeed yet
- `ConnectToEIS` returns `NotSupported`
- no EIS clients are accepted
- no approval flow exists
- no input events are forwarded into `neuswc`

Verification completed:

- `recipe -r ~/.local/share/recipe/recipes isinstalled hevel-desktop`
- `busctl --user introspect org.freedesktop.impl.portal.desktop.hevel /org/freedesktop/portal/desktop`
- `xdg-desktop-portal -r -v` with `XDG_CURRENT_DESKTOP=hevel`

Result:

- Milestone 0 acceptance criteria are met in the scaffold sense:
  - `recipe install hevel-desktop` produces a `hevel` build with portal/EIS
    scaffold support
  - `xdg-desktop-portal` can discover and select the `hevel` backend
- Milestone 1 can start directly from the current recipe-managed state

### Recipes

Update the recipe set to add:

- `libei`
- `xdg-desktop-portal`
- `dbus-devel` if needed by the chosen D-Bus implementation

Rocky-specific note:

- Rocky 10.1 ships `libei` and `libeis` runtime libraries, but not a
  corresponding `-devel` package with headers and pkg-config metadata.
- That means a local source recipe for `libei` or vendored headers will still
  be needed before `hevel` can compile real EIS code against `libeis-1.0`.

Update the `hevel` recipe to:

- add `pkg-config` discovery for `libeis-1.0`
- install the portal metadata files
- install the D-Bus service file
- install a `portals.conf` mapping for `XDG_CURRENT_DESKTOP=hevel`

Deliverables:

- `hevel.portal`
- `org.freedesktop.impl.portal.desktop.hevel.service`
- `hevel-portals.conf`

### Build system

`hevel` currently compiles by including `.c` files into `src/hevel.c`. Keep that
model for now unless it becomes a blocker.

Add new source files:

- `src/portal.c`
- `src/remotedesktop.c`
- `src/inputcapture.c`
- `src/eis.c`

Optional later cleanup:

- convert `hevel` to a normal multi-file build

Acceptance criteria:

- `recipe install hevel-desktop` builds `hevel` with EIS support
- `xdg-desktop-portal` can discover the `hevel` backend

Next action:

- Start Milestone 1A in `neuswc` by adding public keyboard/pointer injection
  wrappers.

## Milestone 1: Make hevel controllable via RemoteDesktop

Status: done on March 10, 2026.

Milestone completion criteria:

- `xdg-desktop-portal` can discover the `hevel` backend
- a client can request keyboard and pointer control
- the local user can explicitly approve or deny the request
- after approval, injected events reach real Wayland clients in `hevel`

### Phase 1: Establish the injection boundary in neuswc

Status: done on March 10, 2026.

Objective:

- expose a stable public API for externally injected keyboard and pointer
  events so `hevel` does not call SWC internals directly

#### Stage 1.1: Define the public injection API

Units of Work:

- add the declaration for `swc_keyboard_inject_key()` to `libswc/swc.h`
- add the declaration for `swc_pointer_inject_relative_motion()` to
  `libswc/swc.h`
- add the declaration for `swc_pointer_inject_absolute_motion()` to
  `libswc/swc.h`
- add the declaration for `swc_pointer_inject_button()` to `libswc/swc.h`
- add the declaration for `swc_pointer_inject_axis()` to `libswc/swc.h`
- add the declaration for `swc_pointer_inject_frame()` to `libswc/swc.h`

Target files:

- `libswc/swc.h`
- `libswc/keyboard.h`
- `libswc/pointer.h`

Design constraints:

- keep the API thin
- align the public API with the existing SWC input model

#### Stage 1.2: Implement the wrappers on top of the existing input path

Units of Work:

- implement the keyboard wrapper in `libswc/keyboard.c`
- implement `swc_pointer_inject_relative_motion()` in `libswc/pointer.c`
- implement `swc_pointer_inject_absolute_motion()` in `libswc/pointer.c`
- implement `swc_pointer_inject_button()` in `libswc/pointer.c`
- implement `swc_pointer_inject_axis()` in `libswc/pointer.c`
- implement `swc_pointer_inject_frame()` in `libswc/pointer.c`
- route the keyboard wrapper into `keyboard_handle_key()`
- route the pointer wrappers into the matching `pointer_handle_*()` functions
- return `false` when `swc` is inactive
- return `false` when the compositor seat is unavailable

Target files:

- `libswc/keyboard.c`
- `libswc/pointer.c`

#### Stage 1.3: Make the change reproducible through recipes

Units of Work:

- apply the recipe-managed patch
  `assets/neuswc-injection/injection.patch`
- add a local `hevel` injection hook that can drive the SWC wrappers inside a
  running compositor process
- add a CLI mode for local testing:
  `hevel --inject <ping|key|motion|absolute|button|axis|frame> ...`
- rebuild `neuswc`
- rebuild `hevel` on top of the rebuilt `neuswc`
- verify the exported symbols in `/usr/local/lib/libswc.a`
- verify the installed declarations in `/usr/local/include/swc.h`

Verification completed:

- `nm -g --defined-only /usr/local/lib/libswc.a`
- installed header check at `/usr/local/include/swc.h`
- full `recipe` rebuild of `neuswc`
- full `recipe` rebuild of `hevel`
- installed binary string check for the `--inject` CLI path

Result:

- the public injection API now exists and is reproducible through the recipes
- `hevel` now has a local control socket hook for test injection
- the remaining Milestone 1 work is now entirely on the `hevel` side

Acceptance criteria:

- a local test hook can inject keyboard and pointer events into a running
  `hevel` session without touching libinput

Runtime note:

- the local test hook is activated by the compositor process on startup
- after installing a new `hevel` build, the session must be restarted before
  the control socket exists in that session

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

### Phase 3: Implement the RemoteDesktop backend session lifecycle

Status: implemented for the v1 auto-approved path.

Objective:

- make the `hevel` backend implement the usable subset of
  `org.freedesktop.impl.portal.RemoteDesktop`

Implementation notes:

- `CreateSession`, `SelectDevices`, `Start`, and `ConnectToEIS` now work in the
  recipe-managed `hevel` overlay
- `Request` and `Session` D-Bus objects are exported on the backend-provided
  object paths
- session lifecycle is enforced as `created -> selected -> started ->
  connected`
- `Start` currently auto-approves and keeps the approval boundary explicit in
  session state for Phase 4
- `ConnectToEIS` bridges from the D-Bus service process to the live compositor
  through the local `hevel-inject-*.sock` handoff path
- when `WAYLAND_DISPLAY` is not present in the D-Bus activation environment,
  the socket resolver falls back to the single live `hevel-inject-*.sock` in
  `XDG_RUNTIME_DIR`

#### Stage 3.1: Add request and session bookkeeping

Units of Work:

- add a `RemoteDesktop` request struct in `hevel` state
- add a `RemoteDesktop` session struct in `hevel` state
- track the portal request handle
- track the portal session handle
- track the caller app id
- track the selected device-type mask
- track approval state
- track EIS-fd issuance state
- track lifecycle state
- add a lookup helper for request handles
- add a lookup helper for session handles
- add a teardown helper for request objects
- add a teardown helper for session objects

Target files:

- `src/portal.c`
- `src/remotedesktop.c`
- `src/hevel.h`

#### Stage 3.2: Implement `CreateSession`

Units of Work:

- parse the incoming request handle
- parse the requested session handle
- validate the required `CreateSession` options
- reject malformed `CreateSession` requests
- reject duplicate session handles
- allocate and store a new session object
- initialize the session lifecycle to created
- return the created session handle in the portal response

Target files:

- `src/portal.c`
- `src/remotedesktop.c`

#### Stage 3.3: Implement `SelectDevices`

Units of Work:

- parse the requested device-type mask
- validate the requested mask against `hevel` Phase 2 capabilities
- reject `SelectDevices` on unknown sessions
- reject `SelectDevices` after an invalid lifecycle transition
- store the selected device mask on the session
- preserve idempotent behavior for repeated compatible selections
- return the selected device mask in the portal response

Target files:

- `src/remotedesktop.c`
- `src/hevel.h`

#### Stage 3.4: Implement `Start`

Units of Work:

- parse the parent window identifier
- parse the `Start` options
- reject `Start` on unknown sessions
- reject `Start` before `CreateSession`
- reject `Start` before `SelectDevices`
- move the session into the started or pending-approval lifecycle state
- return the session result payload expected by the portal contract

Target files:

- `src/remotedesktop.c`
- `src/portal.c`
- `src/hevel.h`

Design constraint:

- keep the `Start` decision boundary explicit so Phase 4 can add real approval
  UI without rewriting the session lifecycle

#### Stage 3.5: Implement `ConnectToEIS`

Units of Work:

- parse the target session handle
- reject `ConnectToEIS` on unknown sessions
- reject `ConnectToEIS` before the session reaches the allowed lifecycle state
- map the selected portal device mask to the existing Phase 2 EIS capability mask
- request a compositor-side EIS client fd from the Phase 2 server
- mark the session as having issued an EIS fd
- return the opened fd in the D-Bus reply

Target files:

- `src/remotedesktop.c`
- `src/eis.c`
- `src/hevel.h`

#### Stage 3.6: Enforce lifecycle and v1 constraints

Units of Work:

- enforce one active `RemoteDesktop` session at a time for v1
- reject invalid lifecycle transitions cleanly
- clean up portal session state on cancellation
- clean up portal session state on disconnect
- clean up any queued EIS state when a portal session is torn down
- make repeated `ConnectToEIS` calls deterministic
- log lifecycle transitions and rejections for debugging

Target files:

- `src/remotedesktop.c`
- `src/portal.c`
- `src/eis.c`
- `src/hevel.h`

Acceptance criteria:

- the backend is discoverable by `xdg-desktop-portal`
- a client can request keyboard and pointer control
- approved sessions receive a working EIS fd

Verification completed:

- `busctl --user introspect` shows `CreateSession`, `SelectDevices`, `Start`,
  and `ConnectToEIS` plus `AvailableDeviceTypes=3` and `version=2`
- direct backend calls succeeded for:
  - `CreateSession`
  - `SelectDevices` with `types=3`
  - `Start`
  - `ConnectToEIS`, which returned a real fd
- repeated `ConnectToEIS` now fails deterministically with
  `RemoteDesktop EIS fd already issued`
- `org.freedesktop.impl.portal.Session.Close` tears down the session cleanly,
  and a new `CreateSession` succeeds afterward

### Phase 4: Add approval UX and end-to-end RemoteDesktop validation

Status: done on March 10, 2026.

Objective:

- make RemoteDesktop control explicit, reviewable, and testable from a user
  session

Current implementation state:

- `src/hevel.h` now carries explicit pending/allowed/denied approval state plus
  prompt tracking on each RemoteDesktop session
- `src/hevel.c` now exposes `hevel --approve-ui ...`
- `src/approve.c` implements the text-only approval helper and returns its
  decision through an inherited `HEVEL_APPROVE_FD`
- `src/remotedesktop.c` no longer auto-approves `Start`; it now moves the
  session into `pending-approval`, spawns a centered `st-wl` helper window,
  waits for `allow` or `deny`, and only then transitions to `started`
- `ConnectToEIS` now rejects pending or denied sessions deterministically
- session teardown now closes any approval prompt still associated with that
  RemoteDesktop session

Verification completed:

- `recipe -r /home/vince/.local/share/recipe/recipes install hevel`
- `recipe -r /home/vince/.local/share/recipe/recipes isinstalled hevel-desktop`
- direct helper check:
  - `printf 'a\n' | HEVEL_APPROVE_FD=<fd> hevel --approve-ui ...` returned
    `allow`
  - `printf 'd\n' | HEVEL_APPROVE_FD=<fd> hevel --approve-ui ...` returned
    `deny`
- rejected backend calls no longer leak `Request` objects on the user bus
- approval prompt rendering now replaces control characters in the displayed app
  id and device summary instead of printing them verbatim
- successful `CreateSession` and `SelectDevices` calls now retire their
  `Request` objects immediately after the reply is sent; the user-bus object
  tree keeps only the live `Session` object
- the compositor-side socket protocol now reserves `eis-open`, `eis-close`,
  `spawn-prepare`, and `spawn-cancel` for the active
  `org.freedesktop.impl.portal.desktop.hevel` process, and raw same-user input
  injection commands are disabled in the installed build
- failed approval prompt setup now rolls back the prepared spawn state instead
  of leaving the next unrelated `st-wl` launch centered as an approval window
- a local validator built on `sd-bus` plus `libei` now exercises the approved
  portal path end to end:
  - `CreateSession -> SelectDevices(types=3) -> Start -> Allow -> ConnectToEIS`
  - keyboard input reaches a real `st-wl` client inside `hevel`
  - relative motion, absolute motion, button, and scroll events reach the live
    `hevel` EIS forwarding path after approval

Runtime note:

- after installing a new `hevel` build, restart the compositor session before
  live approval-path testing so the updated compositor-side socket policy and
  EIS behavior are actually in the running process

#### Stage 4.1: Extend session state for approval flow

Units of Work:

- add an explicit pending-approval session state
- track the requesting application identity on the live session
- track the requested device-type mask on the live session
- track whether an approval prompt is currently visible
- track the final approval decision separately from the session lifecycle

Target files:

- `src/remotedesktop.c`
- `src/hevel.h`

#### Stage 4.2: Add a minimal in-stack approval surface

Implementation path:

- use a centered `st-wl` window as the approval surface
- run `/usr/local/bin/hevel --approve-ui ...` inside that terminal
- keep the helper text-only and ASCII-only: app id, requested device types,
  `Allow [a]`, `Deny [d]`, `Esc = deny`
- return the decision to the portal backend over an inherited one-shot pipe,
  not over a public D-Bus or socket interface

Units of Work:

- add a helper that computes centered approval geometry on the current screen
- launch a dedicated `st-wl` approval window with a stable title
- add a `hevel --approve-ui` helper mode
- render the requesting application identity inside the helper
- render the requested control types inside the helper
- render explicit allow and deny actions inside the helper
- map `a` to allow
- map `d` and `Esc` to deny
- focus the approval window on creation
- close the approval window after a final decision

Target files:

- `src/remotedesktop.c`
- `src/hevel.c`
- `src/hevel.h`

#### Stage 4.3: Wire approval decisions back into the portal backend

Design constraint:

- keep approval transport private to the `Start` call path by using an inherited
  pipe between the backend and `hevel --approve-ui`

Units of Work:

- add a one-shot decision pipe for each approval attempt
- pass the write end of that pipe into `hevel --approve-ui`
- read the final decision from the pipe in `Start`
- move `Start` into pending approval instead of auto-approved
- translate `allow` into an approved started session
- translate `deny` into a rejected session result
- deny cleanly if the approval helper cannot be started

Target files:

- `src/remotedesktop.c`
- `src/hevel.c`
- `src/hevel.h`

#### Stage 4.4: Gate RemoteDesktop operations on approval state

Units of Work:

- reject `ConnectToEIS` before approval has been granted
- return a deterministic backend rejection after denial
- invalidate pending sessions after denial
- preserve successful `ConnectToEIS` behavior after approval
- keep repeated `ConnectToEIS` calls deterministic after approval

Target files:

- `src/remotedesktop.c`
- `src/eis.c`

#### Stage 4.5: Revoke access on denial, close, and session loss

Units of Work:

- tear down any queued EIS request when a pending session is denied
- revoke any live EIS session when an approved session is later closed
- close the approval surface if the portal session is cancelled
- close the approval surface if the compositor session disappears
- log approval, denial, cancellation, and revocation paths for debugging

Target files:

- `src/remotedesktop.c`
- `src/eis.c`
- `src/hevel.h`

#### Stage 4.6: Run end-to-end RemoteDesktop validation

Validation client:

- use a tiny local validator built on `liboeffis`/`libei` rather than `busctl`
  so the Phase 4 check exercises the real frontend portal path

Units of Work:

- request keyboard and pointer control from that client
- approve the request from inside `hevel`
- verify relative motion reaches real clients
- verify absolute motion reaches real clients
- verify buttons and wheel events reach real clients
- verify keyboard events and modifiers reach real clients
- verify deny leaves the target session uncontrollable
- verify `Session.Close` revokes access after approval

Target files:

- runtime validation against the installed stack

Acceptance criteria:

- the user sees who is asking for control
- accept and deny are explicit
- denied sessions do not receive an EIS fd
- closed approved sessions lose access again
- after approval, injected events reach real Wayland clients in `hevel`

### Phase 5: Make `Start` asynchronous and keep the portal backend responsive

Status: done on March 10, 2026.

Objective:

- remove the approval-prompt stall from the backend so pending approval does
  not block unrelated portal traffic

Current implementation state:

- `src/hevel.h` now carries explicit pending-`Start` state for the deferred
  call, including the saved bus message, decision pipe, event sources, and
  prompt process identity
- `src/portal.c` now runs the backend on `sd_event` with the user bus attached
  to that loop instead of the blocking `sd_bus_process` / `sd_bus_wait` path
- `src/remotedesktop.c` now returns from `Start` with a deferred reply, keeps
  the session in `pending-approval`, and resolves the request later from async
  approval, cancellation, timeout, or prompt-exit handlers
- pending `Request.Close` and `Session.Close` now reject the deferred `Start`
  and tear down the session instead of waiting for the approval prompt to
  finish first

Verification completed:

- `recipe -r /home/vince/.local/share/recipe/recipes install hevel`
- `recipe -r /home/vince/.local/share/recipe/recipes isinstalled hevel-desktop`
- with a live pending `Start`, `busctl --user tree
  org.freedesktop.impl.portal.desktop.hevel` still returned immediately while
  the approval prompt was open
- with a live pending `Start`, `Session.Close` returned immediately and the
  blocked `Start` call resolved once with `ua{sv} 2 0`
- with a live pending `Start`, `Request.Close` returned immediately and the
  blocked `Start` call resolved once with `ua{sv} 2 0`
- after each cancellation path, the user-bus object tree returned to just
  `/org/freedesktop/portal/desktop`

Additional verification completed:

- after the async refactor, a real approved `Start` now succeeds end to end:
  - `Start` stayed deferred until approval
  - `ConnectToEIS` returned a working fd on the approved session
  - keyboard input reached a real `st-wl` client
  - relative motion, absolute motion, button, and scroll events were accepted
    by the live compositor on that same approved session

#### Stage 5.1: Add deferred `Start` call state

Units of Work:

- add explicit pending-`Start` call state in `src/hevel.h`
- store the in-flight `sd_bus_message` for a deferred reply
- store the approval decision pipe read fd on that pending call
- store the associated `Request` and `Session` pointers on that pending call
- store prompt process identity needed for later cancellation and cleanup
- track whether the pending call has already been completed

Target files:

- `src/hevel.h`
- `src/remotedesktop.c`

#### Stage 5.2: Move the portal backend to an event-driven service loop

Design constraint:

- keep the new loop compatible with D-Bus activation

Units of Work:

- introduce an `sd_event` loop owned by the portal service
- attach the user bus to that event loop
- replace the current blocking `sd_bus_process` / `sd_bus_wait` loop
- make portal shutdown release the event-loop resources cleanly

Target files:

- `src/portal.c`
- `src/hevel.h`

#### Stage 5.3: Convert `Start` to deferred reply handling

Units of Work:

- change `Start` to return without sending an immediate final reply
- create the pending-call record before launching the approval prompt
- keep the session in `pending-approval` until the async decision arrives
- send the success reply only from the async completion path
- send the rejection reply only from the async completion path
- retire the `Request` object only when the deferred reply is finalized

Target files:

- `src/remotedesktop.c`
- `src/hevel.h`

#### Stage 5.4: Process approval decisions asynchronously

Units of Work:

- register the approval decision pipe with the portal event loop
- read the final `allow` / `deny` decision from that event source
- treat prompt exit without a final decision as denial
- treat pipe close without a final decision as denial
- translate approval into the existing `started` session transition
- translate denial into the existing rejection and teardown path
- remove the event source and close the decision pipe after completion

Target files:

- `src/remotedesktop.c`
- `src/portal.c`

#### Stage 5.5: Handle cancellation, timeout, and teardown safely

Design constraint:

- ensure prompt teardown cannot complete the same deferred call twice

Units of Work:

- cancel a pending `Start` cleanly when `Request.Close` arrives
- cancel a pending `Start` cleanly when `Session.Close` arrives
- deny and clean up if the approval helper cannot be launched
- add an explicit approval timeout and auto-deny on expiry
- ensure session destruction clears any pending async `Start` state

Target files:

- `src/remotedesktop.c`
- `src/portal.c`
- `src/hevel.h`

#### Stage 5.6: Validate concurrent backend behavior

Units of Work:

- verify a pending `Start` no longer blocks `Request.Close`
- verify a pending `Start` no longer blocks `Session.Close`
- verify portal introspection still proceeds while an approval prompt is open
- verify unrelated portal method calls still proceed while an approval prompt is
  open
- verify deny resolves the deferred call exactly once
- verify cancel resolves the deferred call exactly once
- verify timeout resolves the deferred call exactly once
- verify prompt crash resolves the deferred call exactly once
- verify successful approval still permits `ConnectToEIS` afterward

Target files:

- runtime validation against the installed stack

Acceptance criteria:

- an open approval prompt does not freeze the backend
- pending requests can still be cancelled cleanly
- each `Start` call receives exactly one final reply
- approval, denial, cancellation, and timeout all leave no leaked request or
  session state

Planning note:

- Milestone 2 and Milestone 3 are still too coarse in their current form
- before active implementation begins for either one, they should be split the
  same way as Milestone 1:
  - milestone
  - phases
  - stages
  - units of work

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

### Phase 1: Model InputCapture state in hevel

Status: done on March 10, 2026.

Objective:

- add the persistent compositor-side state needed to describe zones, barriers,
  arming, activation, and release

Design constraint:

- keep Phase 1 state-only; do not change live capture behavior yet

#### Stage 1.1: Add InputCapture session state

Units of Work:

- add an InputCapture request struct declaration in `src/hevel.h`
- add an InputCapture session struct declaration in `src/hevel.h`
- add the portal request-handle field to the request struct
- add the portal session-handle field to the session struct
- add the caller app-id field to the session struct
- add the enabled-or-disabled state field to the session struct
- add the active-or-inactive capture state field to the session struct
- add the current activation-id field to the session struct
- add the EIS-fd-issued field to the session struct

Target files:

- `src/hevel.h`
- `src/inputcapture.c`
- `src/portal.c`

#### Stage 1.2: Add zone and barrier state

Units of Work:

- add a zone struct declaration in `src/hevel.h`
- add a pointer-barrier struct declaration in `src/hevel.h`
- add the zone-set serial field in `hevel`
- add the active-zone list head in `hevel`
- add the configured-pointer-barrier list head in `hevel`
- add the active barrier-owner session reference in `hevel`
- add the zone-serial field associated with the installed barriers
- add a stale-barriers flag that marks barriers invalid after a zone change

Target files:

- `src/hevel.h`
- `src/inputcapture.c`

#### Stage 1.3: Recompute zones from the live output layout

Units of Work:

- add a helper that iterates `compositor.screens`
- map one `swc_screen` geometry into one portal-zone record
- build an ordered zone list from the current screen set
- populate the initial zone list on compositor startup
- refresh the zone list after screen add
- refresh the zone list after screen remove
- increment the zone-set serial only when the effective layout changes
- mark installed barriers stale when the zone-set serial changes

Target files:

- `src/window.c`
- `src/inputcapture.c`
- `src/hevel.h`

Acceptance criteria:

- `hevel` can describe its current outputs as an InputCapture zone set
- zone state changes when outputs are added or removed

Verification completed:

- `recipe -r /home/vince/.local/share/recipe/recipes remove hevel`
- `recipe -r /home/vince/.local/share/recipe/recipes install hevel`
- `recipe -r /home/vince/.local/share/recipe/recipes isinstalled hevel-desktop`
- symbol check in `/usr/local/bin/hevel` for:
  - `inputcapture_initialize`
  - `inputcapture_finalize`
  - `inputcapture_refresh_zones`
- source check in the rebuilt tree confirmed:
  - InputCapture state structs are present in `src/hevel.h`
  - `newscreen()` and `screendestroy()` now call
    `inputcapture_refresh_zones()`

Result:

- `hevel` now has persistent InputCapture request, session, zone, and barrier
  state in the overlay
- the compositor now recomputes and versions its zone set when screens are
  added or removed
- barrier staleness can now be tracked against the current zone serial
- no live InputCapture portal behavior is enabled yet; that remains Phase 2

### Phase 2: Implement the InputCapture backend surface in hevel

Status: implemented on March 10, 2026. Full live zone-path verification still
requires one `hevel` session restart so the running compositor process exposes
the new `ic-zones` socket command.

Objective:

- implement the D-Bus backend contract for
  `org.freedesktop.impl.portal.InputCapture`

#### Stage 2.1: Add backend bookkeeping and object export

Units of Work:

- export InputCapture `Request` objects on the backend bus
- export InputCapture `Session` objects on the backend bus
- add lookup helpers for InputCapture requests
- add lookup helpers for InputCapture sessions
- add teardown helpers for InputCapture requests
- add teardown helpers for InputCapture sessions

Target files:

- `src/inputcapture.c`
- `src/portal.c`
- `src/hevel.h`

#### Stage 2.2: Implement `CreateSession` and `GetZones`

Units of Work:

- parse `CreateSession` options
- validate required `CreateSession` option fields
- reject malformed `CreateSession` requests
- reject duplicate InputCapture session handles
- allocate a new InputCapture session object
- return the created session handle
- look up the requested InputCapture session for `GetZones`
- reject unknown or closed InputCapture sessions in `GetZones`
- assemble the `GetZones` response from the current zone set
- return the current zone-set serial from `GetZones`
- return the current zone geometry from `GetZones`

Target files:

- `src/inputcapture.c`
- `src/hevel.h`

#### Stage 2.3: Implement `SetPointerBarriers`

Units of Work:

- parse the requested pointer barriers
- validate that each barrier lies on a current zone edge
- validate that each barrier stays within the bounds of its target zone edge
- reject barriers with invalid coordinates
- reject barriers against stale zone serials
- store the accepted barrier set on the session
- return accepted and failed barrier ids in the portal response

Target files:

- `src/inputcapture.c`
- `src/hevel.h`

#### Stage 2.4: Implement `Enable`, `Disable`, and `Release`

Units of Work:

- implement `Enable` as arming without immediate capture
- implement `Disable` as disarming without forcing activation
- parse `Release` options
- reject `Release` for non-active InputCapture sessions
- record the requested release metadata on the session
- return a successful `Release` response without forcing local pointer motion
- reject lifecycle-invalid method calls cleanly

Target files:

- `src/inputcapture.c`
- `src/hevel.h`

Design constraints:

- keep activation-id handling explicit across enable, activate, and release

#### Stage 2.5: Implement `ConnectToEIS` and session signals

Units of Work:

- look up and validate the requested InputCapture session for `ConnectToEIS`
- map the InputCapture session to the existing EIS server path
- request a compositor-side EIS fd for the validated InputCapture session
- return the EIS fd from `ConnectToEIS`
- reject repeated `ConnectToEIS` calls deterministically
- emit `ZonesChanged` when the zone serial changes
- emit `Activated` when capture begins
- emit `Deactivated` when capture ends normally
- emit `Disabled` when capture is forcibly disarmed

Target files:

- `src/inputcapture.c`
- `src/eis.c`
- `src/portal.c`
- `src/hevel.h`

Acceptance criteria:

- a client can create an InputCapture session and fetch zones
- a client can install barriers for the current layout
- a client can arm capture and obtain an EIS fd for the active session

Verification completed:

- `recipe -r /home/vince/.local/share/recipe/recipes remove hevel`
- `recipe -r /home/vince/.local/share/recipe/recipes install hevel`
- `recipe -r /home/vince/.local/share/recipe/recipes isinstalled hevel-desktop`
- `busctl --user introspect org.freedesktop.impl.portal.desktop.hevel
  /org/freedesktop/portal/desktop org.freedesktop.impl.portal.InputCapture`
- live `CreateSession` returned:
  - `session_id`
  - `capabilities=3`
- live `ConnectToEIS` returned a compositor EIS fd
- live `Session.Close` removed the exported InputCapture session object

Result:

- `hevel` now exports real InputCapture `Request` and `Session` objects on the
  backend bus
- `CreateSession`, `Enable`, `Disable`, `Release`, `ConnectToEIS`, and
  `SetPointerBarriers` are implemented in the overlay
- the portal service now polls the running compositor for zone-set changes and
  emits `ZonesChanged` when its local view changes
- the backend now reports `SupportedCapabilities=3` and `version=1`

Runtime note:

- the running compositor process must be restarted once after this install
  before `GetZones`, `SetPointerBarriers`, and zone-watch-driven
  `ZonesChanged` use the new `ic-zones` bridge in that session

### Phase 3: Add barrier and capture hooks to neuswc

Status: not started.

Objective:

- teach `neuswc` about portal-managed pointer barriers and capture-active state

#### Stage 3.1: Add public capture-control API in SWC

Units of Work:

- add public barrier structs to `libswc/swc.h`
- add a public API for setting active barriers
- add a public API for clearing active barriers
- add a public API for enabling capture mode
- add a public API for disabling capture mode
- add callback hooks from SWC back into `hevel`

Target files:

- `libswc/swc.h`
- `libswc/pointer.h`
- `libswc/swc.c`

#### Stage 3.2: Add barrier storage and validation in the pointer path

Units of Work:

- add barrier storage in the pointer subsystem
- track which edges are armed for capture
- validate barrier direction and geometry at the SWC boundary
- ignore barrier sets when capture is disabled

Target files:

- `libswc/pointer.h`
- `libswc/pointer.c`

#### Stage 3.3: Detect barrier crossing before local clipping

Units of Work:

- test crossing against active barriers before `clip_position()`
- keep current pointer behavior when no barrier is crossed
- stop local pointer progression when a barrier is crossed
- mark the pointer path as capture-active after a crossing
- notify `hevel` that activation should begin

Target files:

- `libswc/pointer.c`

Acceptance criteria:

- local pointer motion still behaves normally with no active barriers
- crossing a configured barrier activates capture instead of clipping locally

### Phase 4: Wire activation and live handoff between hevel and neuswc

Status: not started.

Objective:

- connect the portal backend, SWC barrier crossing, and EIS delivery into one
  live activation path

#### Stage 4.1: Translate portal barrier state into SWC state

Units of Work:

- push accepted barrier sets from `hevel` into SWC
- clear SWC barriers when the portal session is disabled
- clear SWC barriers when the owning portal session is destroyed
- keep only one active barrier owner for v1

Target files:

- `src/inputcapture.c`
- `src/hevel.h`
- `libswc/swc.c`

#### Stage 4.2: Activate capture on SWC barrier crossing

Units of Work:

- receive the SWC activation callback inside `hevel`
- allocate a fresh activation id for the activation event
- transition the InputCapture session into active capture
- emit the portal `Activated` signal with the correct activation id
- begin routing active-session input to the EIS side

Target files:

- `src/inputcapture.c`
- `src/hevel.h`
- `src/eis.c`

#### Stage 4.3: Deactivate cleanly and restore local control

Units of Work:

- stop capture on `Release`
- stop capture on `Disable`
- stop capture on portal-session teardown
- emit `Deactivated` on normal release
- emit `Disabled` on forced disarm
- restore normal local pointer clipping after deactivation

Target files:

- `src/inputcapture.c`
- `src/hevel.h`
- `libswc/pointer.c`

Acceptance criteria:

- activation begins on barrier crossing, not on `Enable`
- deactivation returns local control and clears active capture state

### Phase 5: Handle cursor re-entry and continuity

Status: not started.

Objective:

- make release and local cursor restoration feel continuous instead of abrupt

#### Stage 5.1: Implement release-position mapping

Units of Work:

- parse the portal `cursor_position` release hint
- map the remote release position back into local compositor coordinates
- clamp invalid or out-of-bounds coordinates
- handle release with no cursor hint by falling back to a safe local position

Target files:

- `src/inputcapture.c`
- `libswc/pointer.c`

#### Stage 5.2: Restore the local cursor on release

Units of Work:

- move the local cursor to the chosen re-entry coordinate
- avoid restoring the cursor while capture is still active
- keep release behavior stable across multi-output layouts
- ensure release does not immediately retrigger the same barrier

Target files:

- `src/inputcapture.c`
- `libswc/pointer.c`

Acceptance criteria:

- release returns the cursor to a sane local position
- cursor transitions feel continuous enough for real use

### Phase 6: Validate Milestone 2 behavior end to end

Status: not started.

Objective:

- prove the InputCapture path works across activation, release, and layout
  changes

#### Stage 6.1: Validate the single-output path

Units of Work:

- verify `CreateSession`, `GetZones`, `SetPointerBarriers`, `Enable`, and
  `ConnectToEIS`
- verify barrier crossing activates capture on a single output
- verify `Release` returns local control on a single output

Target files:

- runtime validation against the installed stack

#### Stage 6.2: Validate multi-output and zone-change behavior

Units of Work:

- verify multi-output zone geometry is reported correctly
- verify barriers are rejected after a stale zone serial
- verify `ZonesChanged` is emitted after output-layout changes
- verify active barriers can be reapplied after a zone change

Target files:

- runtime validation against the installed stack

#### Stage 6.3: Validate disable and teardown behavior

Units of Work:

- verify `Disable` ends armed capture cleanly
- verify portal-session close clears active barriers
- verify release and disable do not leak capture state
- verify repeated activation and release remain deterministic

Target files:

- runtime validation against the installed stack

Acceptance criteria:

- the full InputCapture path works on the installed `hevel` stack
- barrier activation, release, disable, and zone changes behave deterministically

## Milestone 3: Hardening

### Keyboard correctness

Tasks:

- preserve modifier state correctly across activation and release
- ensure stuck keys are cleared on disconnect
- ensure session close synthesizes releases where needed

Related code:

- `libswc/keyboard.c`

### Multi-monitor correctness

Tasks:

- verify mixed-resolution outputs
- verify non-rectangular layouts
- verify output hotplug during active capture

### Failure handling

Tasks:

- disconnect EIS client on portal session close
- drop capture state on backend crash or D-Bus disconnect
- reject duplicate active sessions cleanly

### Security and policy

Tasks:

- clearly identify requesting application
- deny by default when no interactive approval path is available
- keep capability selection narrow

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

## Unit or component-level tests

- barrier crossing math
- zone generation from screen topology
- activation id bookkeeping
- stale barrier rejection after zone changes

## Manual tests

### RemoteDesktop

1. Start `hevel`
2. Start `xdg-desktop-portal`
3. Use a test client to request keyboard and pointer control
4. Approve session
5. Verify:
   - pointer motion works
   - button press/release works
   - wheel events work
   - modifiers and repeated keypresses work

### InputCapture

1. Create capture session
2. Fetch zones
3. Install a right-edge pointer barrier
4. Enable capture
5. Move pointer to the barrier
6. Verify:
   - capture activates
   - local cursor stops progressing
   - remote side receives events
   - release restores local control

### Failure tests

- unplug or kill remote client while capture is active
- hotplug a monitor while barriers exist
- restart `xdg-desktop-portal`

## Rough Complexity

- Milestone 1: medium
  - mostly integration work
  - no deep pointer semantics change
- Milestone 2: medium-high
  - requires compositor semantics changes in `neuswc`
  - async portal activation/release state must be correct
- Milestone 3: medium
  - mostly correctness and UX hardening

## Suggested Delivery Order

1. Add SWC injection wrappers
2. Add libeis integration in `hevel`
3. Ship RemoteDesktop backend only
4. Verify Input Leap-style control into `hevel`
5. Add zone generation
6. Add pointer barriers in `neuswc`
7. Ship InputCapture backend
8. Harden multi-monitor and disconnect handling

## Success Criteria

The project is successful when all of the following are true:

- `recipe install hevel-desktop` installs the full portal-enabled stack
- `xdg-desktop-portal` picks the `hevel` backend automatically for
  `XDG_CURRENT_DESKTOP=hevel`
- `hevel` can be remotely controlled through the RemoteDesktop portal using EIS
- `hevel` can participate in seamless edge-triggered input capture using the
  InputCapture portal
- all of the above works without reintroducing GNOME or replacing the
  `wayland.fyi` compositor stack

## References

- XDG Desktop Portal backend guide:
  https://flatpak.github.io/xdg-desktop-portal/docs/writing-a-new-backend.html
- XDG InputCapture backend interface:
  https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.impl.portal.InputCapture.html
- XDG InputCapture frontend interface:
  https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.InputCapture.html
- XDG RemoteDesktop backend interface:
  https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.impl.portal.RemoteDesktop.html
- XDG RemoteDesktop frontend interface:
  https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html
- portals.conf documentation:
  https://flatpak.github.io/xdg-desktop-portal/docs/portals.conf.html
- libeis server API:
  https://libinput.pages.freedesktop.org/libei/api/group__libeis.html
- libei client API:
  https://libinput.pages.freedesktop.org/libei/api/group__libei.html
- liboeffis overview:
  https://libinput.pages.freedesktop.org/libei/api/group__liboeffis.html
