### Phase 1: Establish the injection boundary in neuswc

Status: done on March 10, 2026.

Objective:

- expose a stable public API for externally injected keyboard and pointer
  events so `hevel` does not call SWC internals directly

#### Stage 1.1: Define the public injection API

Units of Work:

- add the declaration for `swc_keyboard_inject_key()` to `libswc/swc.h`
- add the declaration for `swc_pointer_inject_relative_motion()` to
  `libswc/swc.h`
- add the declaration for `swc_pointer_inject_absolute_motion()` to
  `libswc/swc.h`
- add the declaration for `swc_pointer_inject_button()` to `libswc/swc.h`
- add the declaration for `swc_pointer_inject_axis()` to `libswc/swc.h`
- add the declaration for `swc_pointer_inject_frame()` to `libswc/swc.h`

Target files:

- `libswc/swc.h`
- `libswc/keyboard.h`
- `libswc/pointer.h`

Design constraints:

- keep the API thin
- align the public API with the existing SWC input model

#### Stage 1.2: Implement the wrappers on top of the existing input path

Units of Work:

- implement the keyboard wrapper in `libswc/keyboard.c`
- implement `swc_pointer_inject_relative_motion()` in `libswc/pointer.c`
- implement `swc_pointer_inject_absolute_motion()` in `libswc/pointer.c`
- implement `swc_pointer_inject_button()` in `libswc/pointer.c`
- implement `swc_pointer_inject_axis()` in `libswc/pointer.c`
- implement `swc_pointer_inject_frame()` in `libswc/pointer.c`
- route the keyboard wrapper into `keyboard_handle_key()`
- route the pointer wrappers into the matching `pointer_handle_*()` functions
- return `false` when `swc` is inactive
- return `false` when the compositor seat is unavailable

Target files:

- `libswc/keyboard.c`
- `libswc/pointer.c`

#### Stage 1.3: Make the change reproducible through recipes

Units of Work:

- apply the recipe-managed patch
  `assets/neuswc-injection/injection.patch`
- add a local `hevel` injection hook that can drive the SWC wrappers inside a
  running compositor process
- add a CLI mode for local testing:
  `hevel --inject <ping|key|motion|absolute|button|axis|frame> ...`
- rebuild `neuswc`
- rebuild `hevel` on top of the rebuilt `neuswc`
- verify the exported symbols in `/usr/local/lib/libswc.a`
- verify the installed declarations in `/usr/local/include/swc.h`

Verification completed:

- `nm -g --defined-only /usr/local/lib/libswc.a`
- installed header check at `/usr/local/include/swc.h`
- full `recipe` rebuild of `neuswc`
- full `recipe` rebuild of `hevel`
- installed binary string check for the `--inject` CLI path

Result:

- the public injection API now exists and is reproducible through the recipes
- `hevel` now has a local control socket hook for test injection
- the remaining Milestone 1 work is now entirely on the `hevel` side

Acceptance criteria:

- a local test hook can inject keyboard and pointer events into a running
  `hevel` session without touching libinput

Runtime note:

- the local test hook is activated by the compositor process on startup
- after installing a new `hevel` build, the session must be restarted before
  the control socket exists in that session

