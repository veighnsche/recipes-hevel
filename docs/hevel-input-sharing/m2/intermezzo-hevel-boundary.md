### Intermezzo: Architecturally harden the hevel-side InputCapture boundary

Status: planned. Mandatory gate before `M2P3`.

Objective:

- turn the hevel-side InputCapture implementation into a stable subsystem
  boundary before touching `neuswc`, so Phase 3 starts from verified behavior,
  explicit ownership rules, and less incidental coupling

Why this intermezzo exists:

- `M2P2` intentionally landed capability first
- the current hevel-side implementation still mixes protocol glue, zone/barrier
  domain logic, and compositor-bridge transport in ways that will make `M2P3`
  harder to reason about if left as-is
- this is not a polish pass; it is the boundary-hardening step before SWC work

#### Stage I.1: Prove the current runtime invariants

Units of Work:

- restart the running `hevel` session onto the rebuilt compositor
- verify `GetZones` returns the current live zone set
- verify valid outer-edge barriers are accepted
- verify shared internal monitor-edge barriers are rejected
- verify one-pixel-out inclusive-end barriers are rejected
- verify two concurrent InputCapture sessions keep their barrier sets isolated
- verify `ZonesChanged` is emitted after a real output-layout change
- verify disabling or replacing barriers in one session does not perturb another
  session

Target files:

- `src/inputcapture.c`
- `src/inject.c`
- `src/window.c`

#### Stage I.2: Separate domain logic from D-Bus method glue

Units of Work:

- reduce each InputCapture D-Bus method to orchestration and reply shaping only
- move session lookup, lifecycle checks, and transition rules behind explicit
  helper paths
- move barrier installation/replacement logic behind explicit per-session helper
  paths
- move union-boundary geometry validation behind a dedicated barrier-validation
  helper path
- make stale-zone handling explicit instead of distributing it across unrelated
  helpers
- remove any remaining hidden global-state assumptions from the hevel-side code
- extract only the truly shared portal reply/object-export helpers that now
  exist in both `InputCapture` and `RemoteDesktop`, without collapsing
  subsystem-specific lifecycle logic

Target files:

- `src/inputcapture.c`
- `src/remotedesktop.c`
- `src/portal.c`
- `src/hevel.h`

#### Stage I.3: Narrow the compositor-bridge contract

Units of Work:

- define exactly what live state is queried from the compositor versus cached in
  the portal service
- keep the `ic-zones` bridge transport-only inside `inject.c`
- make zone-sync entry points explicit and minimal on the hevel side
- ensure the portal service does not depend on compositor-only internals beyond
  that bridge
- remove incidental coupling between InputCapture zone sync and unrelated inject
  helpers
- make process ownership explicit between:
  - compositor-only runtime state
  - portal-service cached state
  - bridge-transport state

Target files:

- `src/inputcapture.c`
- `src/inject.c`
- `src/hevel.h`
- `src/portal.c`

#### Stage I.4: Reduce overlay fragility at the seam

Units of Work:

- identify full-file overlay copies that exist only to support the current
  InputCapture boundary
- reduce avoidable overlay drift at the hevel/InputCapture seam
- keep hook points for future `neuswc` integration explicit and small
- avoid carrying milestone-history structure into the long-term overlay shape
- reduce the scope of the full-file `window.c` overlay if the only required
  seam is screen-layout change notification
- move portal-side structs and helpers out of the current catch-all header shape
  if they do not need compositor-wide visibility

Target files:

- `src/inputcapture.c`
- `src/window.c`
- `src/hevel.h`
- `src/portal.c`
- `hevel.rhai`

#### Stage I.5: Lock the pre-SWC contract

Units of Work:

- document that session ownership remains a hevel-side invariant
- document that portal lifecycle remains a hevel-side invariant
- document that zone-set authority remains a hevel-side invariant
- document that barrier geometry validation remains a hevel-side invariant
- document that active barrier storage begins in `neuswc` Phase 3
- document that edge-crossing detection begins in `neuswc` Phase 3
- document that capture activation in the pointer path begins in `neuswc`
  Phase 3
- verify `M2P3` can consume the hevel-side barrier/session model without
  reopening the portal contract
- treat this intermezzo as the formal gate for starting `M2P3`

Target files:

- `src/inputcapture.c`
- `src/hevel.h`
- this planning document

Design constraints:

- do not expand the public portal contract during this intermezzo
- prefer smaller, named helper boundaries over another large-file reshuffle
- keep the refactor oriented around ownership, invariants, and future SWC
  integration rather than cosmetic cleanup
- do not turn `inject.c` into the long-term home for unrelated bridge,
  transport, CLI, and test-client logic without explicit boundaries

Acceptance criteria:

- the hevel-side InputCapture backend is live-verified against the rebuilt
  compositor
- the portal method bodies are thinner than the current `M2P2` landing state
- session-local barrier ownership and zone-staleness rules are explicit in code
- the compositor bridge is narrow enough that `M2P3` can consume it without
  further hevel-side protocol churn
- obvious shared portal utilities are not duplicated across `InputCapture` and
  `RemoteDesktop` without reason
- header-level ownership is clearer than the current catch-all `hevel.h` shape
- barrier semantics are correct before any SWC-side capture hooks are added
- the hevel overlay boundary is simpler and clearer than the current
  Phase 2 landing state

