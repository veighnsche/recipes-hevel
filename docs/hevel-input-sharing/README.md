# Hevel Input Sharing Plan

Date: March 10, 2026

This plan is now split by scope so milestone and refactor work can happen in
smaller documents instead of one monolithic file.

## Doc Map

- [Overview](00-overview.md)
  Intro, architecture, current state, and overall implementation direction.
- [Milestone 0: Build And Packaging](01-milestone-0-build-and-packaging.md)
  Bootstrap, portal metadata, build prerequisites, and the initial scaffold.
- [Milestone 1 Index](m1/README.md)
  RemoteDesktop work to make `hevel` controllable by another machine.
- [Milestone 2 Index](m2/README.md)
  InputCapture work for seamless edge-triggered software KVM handoff.
- [Milestone 3: Hardening](03-milestone-3-hardening.md)
  Keyboard correctness, multi-monitor correctness, failure handling, and
  security policy.
- [Detailed File Plan](appendices/detailed-file-plan.md)
  File-by-file ownership and target touch points.
- [Testing And Delivery](appendices/testing-and-delivery.md)
  Manual tests, failure tests, complexity, delivery order, and success
  criteria.
- [References](appendices/references.md)
  Upstream portal and `libei` references.

## Milestone 1

- [Milestone 1 Overview](m1/README.md)
- [Phase 1: Establish The Injection Boundary In `neuswc`](m1/phase-1-neuswc-injection-boundary.md)
- [Phase 2: Add EIS Server Support To `hevel`](m1/phase-2-hevel-eis-server.md)
- [Phase 3: Implement The RemoteDesktop Backend Session Lifecycle](m1/phase-3-remotedesktop-session-lifecycle.md)
- [Phase 4: Add Approval UX And End-To-End RemoteDesktop Validation](m1/phase-4-approval-and-validation.md)
- [Phase 5: Make `Start` Asynchronous](m1/phase-5-async-start.md)

## Milestone 2

- [Milestone 2 Overview](m2/README.md)
- [Phase 1: Model InputCapture State In `hevel`](m2/phase-1-inputcapture-state.md)
- [Phase 2: Implement The InputCapture Backend Surface In `hevel`](m2/phase-2-inputcapture-backend-surface.md)
- [Intermezzo: Architecturally Harden The `hevel`-Side InputCapture Boundary](m2/intermezzo-hevel-boundary.md)
- [Phase 3: Add Barrier And Capture Hooks To `neuswc`](m2/phase-3-neuswc-barrier-and-capture-hooks.md)
- [Phase 4: Wire Activation And Live Handoff Between `hevel` And `neuswc`](m2/phase-4-activation-and-handoff.md)
- [Phase 5: Handle Cursor Re-entry And Continuity](m2/phase-5-cursor-reentry-and-continuity.md)
- [Phase 6: Validate Milestone 2 Behavior End To End](m2/phase-6-validation.md)

## Current Focus

The active planning and refactor gate before `M2P3` is:

- [Intermezzo: Hevel Boundary](m2/intermezzo-hevel-boundary.md)

That is the document to use before starting the `neuswc`-side barrier and
capture work.
