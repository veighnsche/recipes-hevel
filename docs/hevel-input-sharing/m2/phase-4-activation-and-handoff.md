### Phase 4: Wire activation and live handoff between hevel and neuswc

Status: not started.

Objective:

- connect the portal backend, SWC barrier crossing, and EIS delivery into one
  live activation path

#### Stage 4.1: Translate portal barrier state into SWC state

Units of Work:

- push accepted barrier sets from `hevel` into SWC
- clear SWC barriers when the portal session is disabled
- clear SWC barriers when the owning portal session is destroyed
- keep only one active barrier owner for v1

Target files:

- `src/inputcapture.c`
- `src/hevel.h`
- `libswc/swc.c`

#### Stage 4.2: Activate capture on SWC barrier crossing

Units of Work:

- receive the SWC activation callback inside `hevel`
- allocate a fresh activation id for the activation event
- transition the InputCapture session into active capture
- emit the portal `Activated` signal with the correct activation id
- begin routing active-session input to the EIS side

Target files:

- `src/inputcapture.c`
- `src/hevel.h`
- `src/eis.c`

#### Stage 4.3: Deactivate cleanly and restore local control

Units of Work:

- stop capture on `Release`
- stop capture on `Disable`
- stop capture on portal-session teardown
- emit `Deactivated` on normal release
- emit `Disabled` on forced disarm
- restore normal local pointer clipping after deactivation

Target files:

- `src/inputcapture.c`
- `src/hevel.h`
- `libswc/pointer.c`

Acceptance criteria:

- activation begins on barrier crossing, not on `Enable`
- deactivation returns local control and clears active capture state

