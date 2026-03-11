## Milestone 1: Make hevel controllable via RemoteDesktop

Status: done on March 10, 2026.

Milestone completion criteria:

- `xdg-desktop-portal` can discover the `hevel` backend
- a client can request keyboard and pointer control
- the local user can explicitly approve or deny the request
- after approval, injected events reach real Wayland clients in `hevel`

