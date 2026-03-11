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

