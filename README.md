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
M4 now includes ISO9660/UDF inspection, complete WIM/ESD validation, and an
initial Windows writing profile. The application can create a GPT disk with a
FAT32 EFI System Partition, copy and verify the installer without privileges,
and automatically split an oversized `install.wim`. BIOS/MBR and NTFS profiles
remain disabled until they have independent hardware validation.

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
sudo dnf install gcc gettext meson ninja-build gtk4-devel libadwaita-devel glib2-devel udisks2 7zip wimlib-utils util-linux dosfstools
meson setup _build
meson compile -C _build
meson test -C _build --print-errorlogs
./_build/src/linuxusbcreator
```

On Debian or Ubuntu:

```sh
sudo apt install gcc gettext meson ninja-build libgtk-4-dev libadwaita-1-dev libglib2.0-dev udisks2 7zip wimtools fdisk dosfstools
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

To identify an image as Linux, Windows, or raw/unknown:

```sh
./_build/src/linuxusbcreator --inspect-image /path/to/image.iso
```

To inspect an ISO9660/UDF Windows installer without writing any device:

```sh
./_build/src/linuxusbcreator --inspect-windows /path/to/windows.iso
```

For a complete read-only validation of `boot.wim` and `install.wim`/`.esd`:

```sh
./_build/src/linuxusbcreator --validate-windows /path/to/windows.iso
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

For a supported Windows ISO, the graphical interface identifies Windows and
offers `UEFI · GPT` or, for x64 images with BIOS boot files, `BIOS · MBR`.
ARM64 media is restricted to UEFI. The equivalent commands are:

```sh
linuxusbcreator --write-windows IMAGE DEVICE SERIAL SIZE --firmware uefi
linuxusbcreator --write-windows IMAGE DEVICE SERIAL SIZE --firmware bios
```

This command validates the installer before requesting authorization. The
helper only partitions and formats the confirmed USB; ISO extraction, WIM
splitting, synchronization, and verification run as the desktop user.
Every measurable stage reports its own percentage; atomic partitioning,
formatting, mounting, and synchronization report their binary completion.
After verification the Windows target is synchronized and safely unmounted.
An UEFI/GPT target may be hidden by file managers because its partition uses
the standard EFI System GUID; it can be physically disconnected at that point.

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
Windows BIOS boot-record sequences derived from GPL-2.0-or-later `ms-sys` keep
their provenance in `src/third_party/ms-sys/NOTICE`.
