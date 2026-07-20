# Linux USB Creator

Linux USB Creator is a planned native Linux utility for safely creating
bootable USB media from Linux and Windows disk images.

The project is inspired by the workflow and reliability goals of Rufus, but it
is a separate Linux-native application. It is not an official Rufus port and is
not affiliated with the Rufus project.

## Project status

The project is in its architecture and bootstrap phase. **It does not write to
storage devices yet.** See [ROADMAP.md](ROADMAP.md) for the implementation plan
and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the proposed design.

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

## Build and packaging

The exact development dependencies will be finalized with the first executable
milestone. Packaging design is documented in [packaging/README.md](packaging/README.md).

## Safety

Writing an image destroys data on the selected device. No write implementation
will be merged before device identity checks, system-disk exclusion, unmount
handling, confirmation UX, cancellation semantics, and integration tests are in
place.

## Licensing

Original code in this repository is intended to be licensed under GPL-3.0-or-later.
Code adapted from Rufus must retain its original copyright and license notices.

