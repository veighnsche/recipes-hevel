## Milestone 3: Hardening

### Keyboard correctness

Tasks:

- preserve modifier state correctly across activation and release
- ensure stuck keys are cleared on disconnect
- ensure session close synthesizes releases where needed

Related code:

- `libswc/keyboard.c`

### Multi-monitor correctness

Tasks:

- verify mixed-resolution outputs
- verify non-rectangular layouts
- verify output hotplug during active capture

### Failure handling

Tasks:

- disconnect EIS client on portal session close
- drop capture state on backend crash or D-Bus disconnect
- reject duplicate active sessions cleanly

### Security and policy

Tasks:

- clearly identify requesting application
- deny by default when no interactive approval path is available
- keep capability selection narrow

