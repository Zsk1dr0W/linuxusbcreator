# Packaging strategy

Meson will install files into a staging root using `DESTDIR`. Distribution
packages will consume the same release tarball and installed file layout.

## Debian and Ubuntu

`packaging/debian/` will contain `debian/control`, `debian/rules`, copyright,
changelog, and install manifests. CI will build with `dpkg-buildpackage` in a
clean Debian or Ubuntu container and publish `.deb` artifacts.

## Fedora and openSUSE

`packaging/rpm/linuxusbcreator.spec` will build the same tarball with Meson.
CI will use `rpmbuild` in clean Fedora and openSUSE environments and publish
binary and source RPMs.

## Packaging rules

- The GTK application runs as the desktop user.
- Only the dedicated D-Bus helper receives Polkit authorization.
- Packages own no mutable files under `/etc`.
- Maintainer scripts do not enumerate or modify disks.
- Builds run tests but never access host block devices.
- Release artifacts include checksums and an SPDX bill of materials.

