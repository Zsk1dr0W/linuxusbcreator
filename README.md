# Linux USB Creator

Linux USB Creator is a native Linux utility for safely creating
bootable USB media from Linux and Windows disk images.

The project is inspired by the workflow and reliability goals of Rufus, but it
is a separate Linux-native application. It is not an official Rufus port and is
not affiliated with the Rufus project.

## Project status

Milestone M3.1 provides a functional GTK writing workflow on top of the
Polkit-authorized raw image writer. It includes conservative USB selection,
destructive confirmation, live phase/progress reporting, cancellation, full
read-back verification, Spanish localization, desktop/AppStream metadata, a
manual page, reproducible source archives, and install-tested `.deb` and `.rpm`
artifacts. See
[ROADMAP.md](ROADMAP.md) for the implementation plan and
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the proposed design.
M4 is now in progress with read-only ISO9660/UDF inspection for Windows
installers; Windows media creation remains disabled until the partitioning,
copying and WIM-splitting gates are complete.

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
sudo dnf install gcc gettext meson ninja-build gtk4-devel libadwaita-devel glib2-devel udisks2 7zip
meson setup _build
meson compile -C _build
meson test -C _build --print-errorlogs
./_build/src/linuxusbcreator
```

On Debian or Ubuntu:

```sh
sudo apt install gcc gettext meson ninja-build libgtk-4-dev libadwaita-1-dev libglib2.0-dev udisks2 7zip
meson setup _build
meson compile -C _build
meson test -C _build --print-errorlogs
./_build/src/linuxusbcreator
```

For a read-only JSON diagnostic:

```sh
./_build/src/linuxusbcreator --diagnose
```

For a local image checksum (the command only reads the image):

```sh
./_build/src/linuxusbcreator --sha256 /path/to/image.iso
```

To inspect an ISO9660/UDF Windows installer without writing any device:

```sh
./_build/src/linuxusbcreator --inspect-windows /path/to/windows.iso
```

After installation, a confirmed removable USB can be written and fully
verified with:

```sh
linuxusbcreator --write-image IMAGE DEVICE SERIAL SIZE
```

`SERIAL` and `SIZE` must be copied from the immediately preceding
`--diagnose` result. The client invokes a narrowly scoped Polkit helper. The
helper unmounts the target through UDisks2 and revalidates its serial, size,
USB bus, removability, mount state, swap state, and system-disk status before
opening the whole block device exclusively. This command destroys all data on
the target device.

GitHub Actions builds on Debian, Ubuntu, Fedora, and openSUSE, produces `.deb`
and `.rpm` artifacts, and publishes a reproducible source archive plus release
checksums. Packaging details are in
[packaging/README.md](packaging/README.md).

## Safety

Writing an image destroys data on the selected device. The privileged helper
enforces device identity checks, system-disk exclusion, unmount handling,
exclusive access, and cancellation semantics before and during each operation.

## Licensing

Original code in this repository is intended to be licensed under GPL-3.0-or-later.
Code adapted from Rufus must retain its original copyright and license notices.
