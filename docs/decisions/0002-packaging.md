# ADR 0002: Native DEB and RPM packages

Status: accepted for the initial roadmap

## Decision

Publish a source tarball and build native `.deb`, source/binary `.rpm`, and
checksums from tagged releases. Both package families consume the same Meson
install layout and run their builds in clean containers.

Flatpak is not an initial target because raw block-device access and a privileged
system helper do not map cleanly to its sandbox. It can be reconsidered after the
D-Bus interface stabilizes.

## Verification

CI must install, smoke-test, and uninstall each package. Packaging tests must not
expose host block devices to build containers.

