# Architecture

## Principles

1. The graphical process never runs as root.
2. Privileged code is small, non-interactive, and accepts structured requests.
3. A target is identified by stable properties, not only by `/dev/sdX`.
4. The target is revalidated immediately before every destructive transition.
5. Image analysis and operation planning are testable without physical disks.
6. Rufus-derived code is isolated so provenance and future updates remain clear.

## Components

```text
linuxusbcreator (GTK application)
    |
    +-- application/core
    |     image inspection, operation plans, progress, verification
    |
    +-- application/platform-linux
    |     UDisks2 discovery, mount state, device monitoring
    |
    +-- system helper (Polkit-authorized)
          identity validation, exclusive open, partitioning, formatting,
          raw writing, flushing
```

Communication between the application and helper will use a versioned D-Bus
interface. The helper will not accept arbitrary commands, paths to executables,
or shell fragments.

## Proposed source tree

```text
data/                 desktop, AppStream, icons, Polkit and D-Bus files
docs/                 architecture, threat model and decisions
packaging/debian/     Debian package metadata
packaging/rpm/        RPM spec
src/app/              GTK application
src/core/             platform-independent operation engine
src/linux/            Linux discovery and integration
src/helper/           minimal privileged service
tests/unit/            pure unit tests
tests/integration/     loop-device/VM tests
tests/fixtures/        image and UDisks2 metadata fixtures
```

## Device safety state machine

```text
discovered -> eligible -> selected -> confirmed -> authorized
     -> identity-revalidated -> exclusively-opened -> writing
     -> flushed -> verified -> completed
```

Any device removal, identity mismatch, mount-state change, or helper restart
invalidates the operation and requires returning to discovery.

The eligibility policy rejects the root filesystem's backing device and all its
parents, devices containing active swap, mounted non-target filesystems, and
devices not positively identified as removable or externally attached. An
advanced override can be considered only after the MVP and must never allow the
current system disk.

## Rufus reuse boundary

Rufus is GPLv3 and closely coupled to Windows APIs. We will evaluate reusable
algorithms and compatibility data file by file. Directly adapted code must:

- remain GPL-compatible;
- preserve copyright notices;
- identify the upstream file and revision;
- live behind Linux-native interfaces;
- receive Linux-specific tests.

The Win32 GUI, Windows volume management, registry integration, VDS and SetupAPI
layers will be replaced, not emulated.

