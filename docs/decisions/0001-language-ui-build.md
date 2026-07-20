# ADR 0001: C17, GTK4/libadwaita, and Meson

Status: proposed

## Context

The application needs a native Linux desktop UI, direct integration with GLib
D-Bus APIs, a small privileged helper, predictable memory use, and packaging
across Debian- and RPM-based distributions. Select Rufus algorithms may later be
adapted from C.

## Decision

Use C17 for the application, core, and helper; GTK4 with libadwaita for the UI;
and Meson/Ninja for builds and installation.

Keep the operation engine independent of GTK. Expose privileged operations over
a versioned D-Bus interface implemented by a minimal Polkit-authorized service.

## Consequences

- Rufus C algorithms can be evaluated without a language bridge.
- GLib integrates naturally with GTK, D-Bus, cancellation, and device events.
- Meson supports standard `DESTDIR` packaging for both `.deb` and `.rpm`.
- Memory safety requires strict ownership conventions, sanitizers, static
  analysis, parser fuzzing, and focused review of the privileged boundary.

Before M1 implementation, this ADR may be superseded if a small prototype shows
that Rust materially reduces risk without impairing packaging or Rufus-code reuse.

