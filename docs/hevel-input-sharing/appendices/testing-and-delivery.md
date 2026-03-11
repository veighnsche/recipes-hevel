
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

