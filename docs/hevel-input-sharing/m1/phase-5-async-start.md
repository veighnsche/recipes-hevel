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

