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

