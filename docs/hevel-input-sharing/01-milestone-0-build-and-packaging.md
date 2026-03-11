## Milestone 0: Build and packaging prerequisites

Status: done on March 10, 2026.

What landed:

- `rocky-hevel-bootstrap.rhai` now installs the build/runtime prerequisites
  needed for the portal/EIS path:
  - `dbus-devel`
  - `libei`
  - `libeis`
  - `xdg-desktop-portal`
  - `meson`
  - `ninja-build`
  - `python3-jinja2`
  - `python3-attrs`
- `libei-local.rhai` now builds and installs `libei`/`libeis` 1.3.0 from
  source into `/usr/local`, including headers and `pkg-config` metadata, to
  cover Rocky 10.1's missing `-devel` package.
- `hevel.rhai` now rebuilds `hevel` from a recipe-managed source overlay that
  adds:
  - `src/portal.c`
  - `src/remotedesktop.c`
  - `src/inputcapture.c`
  - `src/eis.c`
  - `--portal-service` mode in `hevel`
- `hevel-portal-scaffold.rhai` now installs live backend discovery metadata
  into:
  - `/usr/share/xdg-desktop-portal/portals/hevel.portal`
  - `/usr/share/dbus-1/services/org.freedesktop.impl.portal.desktop.hevel.service`
  - `/usr/share/xdg-desktop-portal/hevel-portals.conf`
- Old failed metadata in `/usr/local/share` is removed as part of the scaffold
  recipe so discovery stays deterministic.

What the scaffold currently does:

- Exports `org.freedesktop.impl.portal.InputCapture` version `1`
- Exports `org.freedesktop.impl.portal.RemoteDesktop` version `2`
- Activates on the user session bus via `hevel --portal-service`
- Is discoverable by `xdg-desktop-portal` when
  `XDG_CURRENT_DESKTOP=hevel`

What the scaffold does not do yet:

- `CreateSession`, `GetZones`, `SetPointerBarriers`, `Enable`, `Disable`,
  `Release`, `SelectDevices`, and `Start` do not succeed yet
- `ConnectToEIS` returns `NotSupported`
- no EIS clients are accepted
- no approval flow exists
- no input events are forwarded into `neuswc`

Verification completed:

- `recipe -r ~/.local/share/recipe/recipes isinstalled hevel-desktop`
- `busctl --user introspect org.freedesktop.impl.portal.desktop.hevel /org/freedesktop/portal/desktop`
- `xdg-desktop-portal -r -v` with `XDG_CURRENT_DESKTOP=hevel`

Result:

- Milestone 0 acceptance criteria are met in the scaffold sense:
  - `recipe install hevel-desktop` produces a `hevel` build with portal/EIS
    scaffold support
  - `xdg-desktop-portal` can discover and select the `hevel` backend
- Milestone 1 can start directly from the current recipe-managed state

### Recipes

Update the recipe set to add:

- `libei`
- `xdg-desktop-portal`
- `dbus-devel` if needed by the chosen D-Bus implementation

Rocky-specific note:

- Rocky 10.1 ships `libei` and `libeis` runtime libraries, but not a
  corresponding `-devel` package with headers and pkg-config metadata.
- That means a local source recipe for `libei` or vendored headers will still
  be needed before `hevel` can compile real EIS code against `libeis-1.0`.

Update the `hevel` recipe to:

- add `pkg-config` discovery for `libeis-1.0`
- install the portal metadata files
- install the D-Bus service file
- install a `portals.conf` mapping for `XDG_CURRENT_DESKTOP=hevel`

Deliverables:

- `hevel.portal`
- `org.freedesktop.impl.portal.desktop.hevel.service`
- `hevel-portals.conf`

### Build system

`hevel` currently compiles by including `.c` files into `src/hevel.c`. Keep that
model for now unless it becomes a blocker.

Add new source files:

- `src/portal.c`
- `src/remotedesktop.c`
- `src/inputcapture.c`
- `src/eis.c`

Optional later cleanup:

- convert `hevel` to a normal multi-file build

Acceptance criteria:

- `recipe install hevel-desktop` builds `hevel` with EIS support
- `xdg-desktop-portal` can discover the `hevel` backend

Next action:

- Start Milestone 1A in `neuswc` by adding public keyboard/pointer injection
  wrappers.

