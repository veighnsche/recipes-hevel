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

