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

