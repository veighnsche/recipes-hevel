### Phase 6: Validate Milestone 2 behavior end to end

Status: not started.

Objective:

- prove the InputCapture path works across activation, release, and layout
  changes

#### Stage 6.1: Validate the single-output path

Units of Work:

- verify `CreateSession`, `GetZones`, `SetPointerBarriers`, `Enable`, and
  `ConnectToEIS`
- verify barrier crossing activates capture on a single output
- verify `Release` returns local control on a single output

Target files:

- runtime validation against the installed stack

#### Stage 6.2: Validate multi-output and zone-change behavior

Units of Work:

- verify multi-output zone geometry is reported correctly
- verify barriers are rejected after a stale zone serial
- verify `ZonesChanged` is emitted after output-layout changes
- verify active barriers can be reapplied after a zone change

Target files:

- runtime validation against the installed stack

#### Stage 6.3: Validate disable and teardown behavior

Units of Work:

- verify `Disable` ends armed capture cleanly
- verify portal-session close clears active barriers
- verify release and disable do not leak capture state
- verify repeated activation and release remain deterministic

Target files:

- runtime validation against the installed stack

Acceptance criteria:

- the full InputCapture path works on the installed `hevel` stack
- barrier activation, release, disable, and zone changes behave deterministically

