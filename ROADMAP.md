# Roadmap

This roadmap uses safety gates rather than dates. A milestone is complete only
when its acceptance criteria pass on supported distributions.

## M0 — Repository and design

- [x] Define scope, architecture, security model, and packaging approach.
- [x] Establish GPL-compatible contribution and attribution rules.
- [x] Add an initial application icon and desktop metadata plan.
- [ ] Enable GitHub Actions and branch protection.
- [ ] Record architecture decisions as ADRs.

Exit criteria: repository builds documentation cleanly and project boundaries
are explicit.

## M1 — Read-only device discovery

- [ ] GTK application shell and command-line diagnostic mode.
- [ ] Enumerate block devices using UDisks2.
- [ ] Display stable identity: `/dev` node, model, serial, bus, and capacity.
- [ ] Exclude loop, zram, device-mapper, optical, and system/root devices.
- [ ] React safely to device insertion, removal, and renumbering.
- [ ] Unit tests using captured UDisks2 fixtures.

Exit criteria: no code path opens a block device for writing.

## M2 — Raw/hybrid image writer MVP

- [ ] Inspect images and compute SHA-256.
- [ ] Acquire authorization through a narrowly scoped Polkit helper.
- [ ] Unmount target partitions and revalidate device identity.
- [ ] Stream writes with bounded memory, progress, cancellation, and `fsync`.
- [ ] Optional full read-back verification.
- [ ] Structured and exportable operation log.
- [ ] Failure-injection tests for short writes, disconnects, and permission loss.

Exit criteria: verified raw writes work on the hardware test matrix without
allowing the current system disk as a target.

## M3 — First distributable release

- [ ] AppStream metadata, `.desktop` file, icons, translations, and man page.
- [ ] Reproducible source tarball.
- [ ] CI jobs producing `.deb` and `.rpm` artifacts.
- [ ] Package installation/removal tests on Debian, Ubuntu, Fedora, and openSUSE.
- [ ] Signed release checksums and documented threat model.

Exit criteria: packages install cleanly and the MVP works without launching the
GUI as root.

## M4 — Windows installation media

- [ ] Inspect ISO9660/UDF filesystems.
- [ ] Create GPT/MBR layouts for BIOS and UEFI targets.
- [ ] Format FAT32/NTFS using controlled helpers.
- [ ] Copy files while preserving relevant metadata.
- [ ] Split oversized `install.wim` files with wimlib.
- [ ] Validate boot structures and test current Windows installer images.

## M5 — Linux ISO mode and persistence

- [ ] ISO extraction mode for images that cannot use raw-copy mode.
- [ ] Syslinux/GRUB handling where required.
- [ ] Optional persistence for explicitly supported distributions.
- [ ] Compatibility database with fixtures and provenance.

## Later candidates

- Save a USB device to an image.
- Bad-block and counterfeit-capacity testing.
- Download and checksum workflows.
- Accessibility and broader localization.

