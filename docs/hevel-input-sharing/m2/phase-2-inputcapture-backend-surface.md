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

