### Phase 3: Add barrier and capture hooks to neuswc

Status: not started.

Objective:

- teach `neuswc` about portal-managed pointer barriers and capture-active state

#### Stage 3.1: Add public capture-control API in SWC

Units of Work:

- add public barrier structs to `libswc/swc.h`
- add a public API for setting active barriers
- add a public API for clearing active barriers
- add a public API for enabling capture mode
- add a public API for disabling capture mode
- add callback hooks from SWC back into `hevel`

Target files:

- `libswc/swc.h`
- `libswc/pointer.h`
- `libswc/swc.c`

#### Stage 3.2: Add barrier storage and validation in the pointer path

Units of Work:

- add barrier storage in the pointer subsystem
- track which edges are armed for capture
- validate barrier direction and geometry at the SWC boundary
- ignore barrier sets when capture is disabled

Target files:

- `libswc/pointer.h`
- `libswc/pointer.c`

#### Stage 3.3: Detect barrier crossing before local clipping

Units of Work:

- test crossing against active barriers before `clip_position()`
- keep current pointer behavior when no barrier is crossed
- stop local pointer progression when a barrier is crossed
- mark the pointer path as capture-active after a crossing
- notify `hevel` that activation should begin

Target files:

- `libswc/pointer.c`

Acceptance criteria:

- local pointer motion still behaves normally with no active barriers
- crossing a configured barrier activates capture instead of clipping locally

