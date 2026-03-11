## Intermezzo Companion: Large-file refactor program for `hevel-overlay/src`

Status: in progress. `R1` and `R2` are complete. Mandatory gate before
`M2P3`.

Objective:

- refactor every currently oversized source file in
  `/home/vince/.local/share/recipe/recipes/assets/hevel-overlay/src` so each
  resulting `.c` file is below `500` lines of code
- make the file split follow ownership and subsystem boundaries rather than
  arbitrary line-count chopping

Scope:

- baseline oversized files at refactor start:
  - `src/inputcapture.c` (`1839`)
  - `src/inject.c` (`1523`)
  - `src/remotedesktop.c` (`1340`)
  - `src/eis.c` (`832`)
- remaining oversized `.c` files after `R2`:
  - `src/inject.c` (`1524`)
  - `src/remotedesktop.c` (`1340`)
  - `src/eis.c` (`832`)
- files already below `500` lines are not forced to move unless they are
  needed to support a cleaner ownership boundary

Why this exists:

- the current hevel overlay landed capability first
- the main maintenance risk is no longer missing behavior; it is oversized
  subsystem files with mixed ownership
- `M2P3` should not start on top of multi-thousand-line files that still mix
  transport, protocol glue, domain logic, approval flow, and event forwarding

Success criteria:

- no `.c` file in `assets/hevel-overlay/src` exceeds `500` lines
- file splits follow explicit subsystem ownership
- internal helpers are grouped by reusable responsibilities, not milestone
  history
- shared portal helpers are centralized only where reuse is real
- refactoring does not widen the public portal contract
- `recipe install hevel` still produces a working build after each phase

Design constraints:

- do not game the count with comment churn or formatting tricks
- prefer cohesive internal modules plus small internal headers
- keep `hevel.c` as the top-level include aggregator unless the build model
  itself becomes the blocker
- do not move ownership from `hevel` to `neuswc` in this refactor
- do not use this refactor to sneak in new user-visible features

### Phase R1: Freeze the internal ownership map

Status: done on March 11, 2026.

Objective:

- define the stable internal module boundaries before moving code

#### Stage R1.1: Inventory and target map

Units of Work:

- record the current line counts for all files in `src`
- mark the four oversized files as mandatory refactor targets
- define the target file map for `inputcapture`
- define the target file map for `inject`
- define the target file map for `remotedesktop`
- define the target file map for `eis`

Target files:

- this planning document
- `appendices/detailed-file-plan.md`

Completed inventory:

- `src/portal.c`: `130`
- `src/approve.c`: `160`
- `src/hevel.c`: `203`
- `src/hevel.h`: `324`
- `src/window.c`: `352`
- `src/eis.c`: `832`
- `src/remotedesktop.c`: `1340`
- `src/inject.c`: `1523`
- `src/inputcapture.c`: `1839`

Mandatory refactor targets locked in this phase:

- `src/inputcapture.c`
- `src/inject.c`
- `src/remotedesktop.c`
- `src/eis.c`

#### Stage R1.2: Internal header plan

Units of Work:

- identify which declarations can leave `src/hevel.h`
- define one internal header for InputCapture-only declarations
- define one internal header for RemoteDesktop-only declarations
- define one internal header for bridge and inject-only declarations
- define one internal header for EIS-only declarations

Target files:

- `src/hevel.h`
- future internal headers under `src/`

Chosen header split:

- `src/inputcapture.h`
  - owns InputCapture request/session structs, zone and barrier structs,
    InputCapture-local enums, and non-public helper declarations shared across
    the split InputCapture modules
- `src/remotedesktop.h`
  - owns RemoteDesktop session/request structs, pending-approval state, session
    state enums, and non-public helper declarations shared across the split
    RemoteDesktop modules
- `src/inject.h`
  - owns bridge transport declarations, spawn-bridge declarations, zone-bridge
    declarations, CLI entrypoints, and inject-side utility declarations shared
    across the split inject modules
- `src/eis.h`
  - owns EIS request/client structs, capability and device declarations, and
    non-public helper declarations shared across the split EIS modules

`src/hevel.h` ownership after the refactor:

- keep compositor-global state and top-level startup/shutdown entrypoints
- keep only declarations that truly cross subsystem boundaries
- move subsystem-private portal, InputCapture, RemoteDesktop, inject, and EIS
  declarations into their internal headers

Phase R1 result:

- the oversized-file target set is now fixed
- the replacement file map for each oversized subsystem is now fixed
- the internal-header ownership split is now fixed
- the next refactor phases can move code without reopening the ownership model

### Phase R2: Split `inputcapture.c` below `500` lines per file

Status: done on March 11, 2026.

Objective:

- turn `InputCapture` into smaller modules with clear ownership boundaries

Target file map:

- `src/inputcapture-session.c`
- `src/inputcapture-zones.c`
- `src/inputcapture-barriers.c`
- `src/inputcapture-methods.c`
- `src/inputcapture-signals.c`
- `src/inputcapture.h`

Implemented result:

- `src/inputcapture.c`: `109`
- `src/inputcapture-session.c`: `285`
- `src/inputcapture-zones.c`: `259`
- `src/inputcapture-barriers.c`: `321`
- `src/inputcapture-methods.c`: `487`
- `src/inputcapture-signals.c`: `309`
- `src/inputcapture.h`: `276`
- `src/hevel.h`: reduced from `324` to `257` after moving InputCapture-private
  declarations out

#### Stage R2.1: Extract session and request lifecycle

Units of Work:

- move request lookup and teardown helpers into `inputcapture-session.c`
- move session lookup and teardown helpers into `inputcapture-session.c`
- move session-id generation and lifecycle state helpers into
  `inputcapture-session.c`
- move session-local barrier ownership helpers into `inputcapture-session.c`

Target files:

- `src/inputcapture.c`
- `src/inputcapture-session.c`
- `src/inputcapture.h`

#### Stage R2.2: Extract zone and barrier domain logic

Units of Work:

- move zone-list allocation and destruction into `inputcapture-zones.c`
- move compositor zone sync and zone-watch logic into `inputcapture-zones.c`
- move barrier parsing helpers into `inputcapture-barriers.c`
- move union-boundary validation into `inputcapture-barriers.c`
- move stale-zone and serial validation into `inputcapture-barriers.c`

Target files:

- `src/inputcapture.c`
- `src/inputcapture-zones.c`
- `src/inputcapture-barriers.c`
- `src/inputcapture.h`

#### Stage R2.3: Extract D-Bus methods and signals

Units of Work:

- move reply payload builders into `inputcapture-signals.c`
- keep `CreateSession` and `GetZones` orchestration in `inputcapture-methods.c`
- keep `SetPointerBarriers`, `Enable`, `Disable`, `Release`, and
  `ConnectToEIS` orchestration in `inputcapture-methods.c`
- leave method bodies thin enough that they mainly validate, call helpers, and
  shape replies

Target files:

- `src/inputcapture.c`
- `src/inputcapture-methods.c`
- `src/inputcapture-signals.c`
- `src/inputcapture.h`

#### Stage R2.4: Retire the old file and enforce budgets

Units of Work:

- reduce the remaining `src/inputcapture.c` to a thin entrypoint or remove it
- confirm every resulting InputCapture file is below `500` lines
- document final InputCapture ownership in `appendices/detailed-file-plan.md`

Target files:

- `src/inputcapture.c`
- `src/inputcapture-session.c`
- `src/inputcapture-zones.c`
- `src/inputcapture-barriers.c`
- `src/inputcapture-methods.c`
- `src/inputcapture-signals.c`
- `appendices/detailed-file-plan.md`

Verification completed:

- `recipe -r /home/vince/.local/share/recipe/recipes remove hevel`
- `recipe -r /home/vince/.local/share/recipe/recipes install hevel`
- `recipe -r /home/vince/.local/share/recipe/recipes isinstalled hevel-desktop`
- line-count check for the split InputCapture files under
  `assets/hevel-overlay/src`

Phase R2 result:

- `inputcapture.c` is now a thin aggregator and vtable layer
- InputCapture-private declarations now live in `src/inputcapture.h`
- `inject.c` now consumes the InputCapture internal header instead of leaning
  on the old catch-all `hevel.h`
- every InputCapture `.c` file is now below `500` lines

### Phase R3: Split `inject.c` below `500` lines per file

Objective:

- isolate bridge transport, authorization, spawn control, and CLI/test paths

Target file map:

- `src/inject-transport.c`
- `src/inject-auth.c`
- `src/inject-spawn.c`
- `src/inject-zones.c`
- `src/inject-cli.c`
- `src/eis-cli.c`
- `src/inject.h`

#### Stage R3.1: Extract socket transport and peer authorization

Units of Work:

- move socket connect, accept, and reply helpers into `inject-transport.c`
- move peer-pid lookup and portal-owner authorization into `inject-auth.c`
- keep the transport path free of InputCapture or approval policy

Target files:

- `src/inject.c`
- `src/inject-transport.c`
- `src/inject-auth.c`
- `src/inject.h`

#### Stage R3.2: Extract spawn and zone bridge helpers

Units of Work:

- move spawn-prepare and spawn-cancel handling into `inject-spawn.c`
- move `ic-zones` parsing and reply assembly into `inject-zones.c`
- keep the compositor bridge transport-only after extraction

Target files:

- `src/inject.c`
- `src/inject-spawn.c`
- `src/inject-zones.c`
- `src/inject.h`

#### Stage R3.3: Extract CLI entrypoints

Units of Work:

- move the non-EIS local CLI path into `inject-cli.c`
- move the EIS client CLI path into `eis-cli.c`
- keep test-client behavior out of the compositor bridge implementation

Target files:

- `src/inject.c`
- `src/inject-cli.c`
- `src/eis-cli.c`
- `src/inject.h`

#### Stage R3.4: Retire the old file and enforce budgets

Units of Work:

- reduce the remaining `src/inject.c` to a thin entrypoint or remove it
- confirm every resulting inject-related `.c` file is below `500` lines
- document final bridge and CLI ownership in `appendices/detailed-file-plan.md`

Target files:

- `src/inject.c`
- `src/inject-transport.c`
- `src/inject-auth.c`
- `src/inject-spawn.c`
- `src/inject-zones.c`
- `src/inject-cli.c`
- `src/eis-cli.c`
- `appendices/detailed-file-plan.md`

### Phase R4: Split `remotedesktop.c` below `500` lines per file

Objective:

- separate portal session lifecycle, approval flow, and D-Bus method glue

Target file map:

- `src/remotedesktop-session.c`
- `src/remotedesktop-approval.c`
- `src/remotedesktop-methods.c`
- `src/remotedesktop-signals.c`
- `src/remotedesktop.h`

#### Stage R4.1: Extract session and request lifecycle

Units of Work:

- move request/session lookup and teardown into `remotedesktop-session.c`
- move session-id generation and state-transition helpers into
  `remotedesktop-session.c`
- keep lifecycle invariants centralized in one module

Target files:

- `src/remotedesktop.c`
- `src/remotedesktop-session.c`
- `src/remotedesktop.h`

#### Stage R4.2: Extract approval flow

Units of Work:

- move prompt launch and stop helpers into `remotedesktop-approval.c`
- move approval-decision read and pending-start cleanup into
  `remotedesktop-approval.c`
- keep asynchronous `Start` resolution out of the D-Bus method layer

Target files:

- `src/remotedesktop.c`
- `src/remotedesktop-approval.c`
- `src/remotedesktop.h`

#### Stage R4.3: Extract D-Bus methods and signals

Units of Work:

- move response payload builders into `remotedesktop-signals.c`
- keep `CreateSession`, `SelectDevices`, `Start`, and `ConnectToEIS`
  orchestration in `remotedesktop-methods.c`
- leave method bodies thin enough that they mainly validate, call helpers, and
  shape replies

Target files:

- `src/remotedesktop.c`
- `src/remotedesktop-methods.c`
- `src/remotedesktop-signals.c`
- `src/remotedesktop.h`

#### Stage R4.4: Retire the old file and enforce budgets

Units of Work:

- reduce the remaining `src/remotedesktop.c` to a thin entrypoint or remove it
- confirm every resulting RemoteDesktop file is below `500` lines
- document final RemoteDesktop ownership in
  `appendices/detailed-file-plan.md`

Target files:

- `src/remotedesktop.c`
- `src/remotedesktop-session.c`
- `src/remotedesktop-approval.c`
- `src/remotedesktop-methods.c`
- `src/remotedesktop-signals.c`
- `appendices/detailed-file-plan.md`

### Phase R5: Split `eis.c` below `500` lines per file

Objective:

- separate EIS server lifecycle, device advertisement, and event forwarding

Target file map:

- `src/eis-core.c`
- `src/eis-devices.c`
- `src/eis-forward.c`
- `src/eis.h`

#### Stage R5.1: Extract server and request lifecycle

Units of Work:

- move request queue and client cleanup into `eis-core.c`
- move event-loop bootstrap and teardown into `eis-core.c`
- keep client-connect and disconnect ownership explicit

Target files:

- `src/eis.c`
- `src/eis-core.c`
- `src/eis.h`

#### Stage R5.2: Extract device setup

Units of Work:

- move capability mapping and device creation helpers into `eis-devices.c`
- keep keyboard, absolute, and pointer device advertisement in one place

Target files:

- `src/eis.c`
- `src/eis-devices.c`
- `src/eis.h`

#### Stage R5.3: Extract event forwarding

Units of Work:

- move key forwarding into `eis-forward.c`
- move relative and absolute motion forwarding into `eis-forward.c`
- move button and scroll forwarding into `eis-forward.c`
- move frame dispatch and event-type routing into `eis-forward.c`

Target files:

- `src/eis.c`
- `src/eis-forward.c`
- `src/eis.h`

#### Stage R5.4: Retire the old file and enforce budgets

Units of Work:

- reduce the remaining `src/eis.c` to a thin entrypoint or remove it
- confirm every resulting EIS file is below `500` lines
- document final EIS ownership in `appendices/detailed-file-plan.md`

Target files:

- `src/eis.c`
- `src/eis-core.c`
- `src/eis-devices.c`
- `src/eis-forward.c`
- `appendices/detailed-file-plan.md`

### Phase R6: Lock the post-refactor file-budget rule

Objective:

- make sub-`500` source files a maintained invariant instead of a one-off
  cleanup

#### Stage R6.1: Validate the new structure

Units of Work:

- run line-count verification for all files in `src`
- confirm no `.c` file exceeds `500` lines
- confirm no new catch-all file replaced the old catch-all files
- verify the build still succeeds through `recipe`

Target files:

- `src/*`
- `hevel.rhai`

#### Stage R6.2: Record the new rule in the plan

Units of Work:

- update `appendices/detailed-file-plan.md` to reflect the final module layout
- update the Intermezzo document to treat the sub-`500` file budget as met
- record the file-budget rule as a standing refactor invariant for future work

Target files:

- `appendices/detailed-file-plan.md`
- `m2/intermezzo-hevel-boundary.md`
- this planning document
