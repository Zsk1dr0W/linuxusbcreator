# Linux USB Creator

Linux USB Creator is a planned native Linux utility for safely creating
bootable USB media from Linux and Windows disk images.

The project is inspired by the workflow and reliability goals of Rufus, but it
is a separate Linux-native application. It is not an official Rufus port and is
not affiliated with the Rufus project.

## Project status

Milestone M1 provides a read-only GTK application and a diagnostic command that
enumerate storage through UDisks2. **It does not write to storage devices.** See
[ROADMAP.md](ROADMAP.md) for the implementation plan and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the proposed design.

## Initial goals

- Detect removable USB devices conservatively.
- Write hybrid ISO/raw images with progress, cancellation, and verification.
- Create Windows installation media, including FAT32-compatible WIM splitting.
- Support BIOS and UEFI boot targets.
- Integrate with UDisks2 and Polkit instead of running the UI as root.
- Produce native `.deb` and `.rpm` packages in CI.
- Provide clear logs and strong safeguards against selecting a system disk.

## Non-goals for the first release

- Feature parity with Rufus.
- Windows To Go.
- Multiboot media.
- Writing internal system drives.
- Running the graphical application as root.

## Proposed technology

- C17
- GTK4 and libadwaita
- Meson and Ninja
- GLib/GIO, UDisks2, Polkit, libfdisk and wimlib
- AppStream and freedesktop desktop integration

## Build

On Fedora:

```sh
sudo dnf install gcc meson ninja-build gtk4-devel libadwaita-devel glib2-devel udisks2
meson setup _build
meson compile -C _build
meson test -C _build --print-errorlogs
./_build/src/linuxusbcreator
```

On Debian or Ubuntu:

```sh
sudo apt install gcc meson ninja-build libgtk-4-dev libadwaita-1-dev libglib2.0-dev udisks2
meson setup _build
meson compile -C _build
meson test -C _build --print-errorlogs
./_build/src/linuxusbcreator
```

For a read-only JSON diagnostic:

```sh
./_build/src/linuxusbcreator --diagnose
```

GitHub Actions builds on Ubuntu and Fedora and produces `.deb` and `.rpm`
artifacts. Packaging details are in [packaging/README.md](packaging/README.md).

## Safety

Writing an image destroys data on the selected device. No write implementation
will be merged before device identity checks, system-disk exclusion, unmount
handling, confirmation UX, cancellation semantics, and integration tests are in
place.

## Licensing

Original code in this repository is intended to be licensed under GPL-3.0-or-later.
Code adapted from Rufus must retain its original copyright and license notices.
