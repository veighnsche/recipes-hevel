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

