Name:           linuxusbcreator
Version:        0.7.0
Release:        1%{?dist}
Summary:        Native Linux utility for creating bootable USB media
License:        GPL-3.0-or-later AND GPL-2.0-or-later
URL:            https://github.com/Zsk1dr0W/linuxusbcreator
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  gcc
BuildRequires:  gettext
BuildRequires:  meson
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib
BuildRequires:  pkgconfig(gio-2.0) >= 2.72
BuildRequires:  pkgconfig(gio-unix-2.0) >= 2.72
BuildRequires:  pkgconfig(gtk4) >= 4.8
BuildRequires:  pkgconfig(libadwaita-1) >= 1.2
BuildRequires:  pkgconfig(libselinux)
Requires:       udisks2
Requires:       polkit
Requires:       /usr/bin/7z
Requires:       /usr/bin/wimlib-imagex
Requires:       /usr/bin/sfdisk
Requires:       /usr/bin/mkfs.fat
Requires:       /usr/sbin/mkfs.ext4

%description
Linux USB Creator is a native Linux utility for safely inspecting removable USB
devices, writing verified raw images, and creating UEFI/GPT or BIOS/MBR
Windows installation media and extracted Fedora Live UEFI media with optional
OverlayFS persistence, per-stage progress and complete file verification.

%prep
%autosetup

%build
%meson
%meson_build

%install
%meson_install

%check
%meson_test

%files
%license LICENSE
%doc README.md ROADMAP.md src/third_party/ms-sys/NOTICE
%{_bindir}/linuxusbcreator
%{_libexecdir}/linuxusbcreator-helper
%{_datadir}/applications/io.github.zsk1dr0w.LinuxUsbCreator.desktop
%{_datadir}/metainfo/io.github.zsk1dr0w.LinuxUsbCreator.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/io.github.zsk1dr0w.LinuxUsbCreator.png
%{_datadir}/polkit-1/actions/io.github.zsk1dr0w.LinuxUsbCreator.policy
%{_datadir}/locale/*/LC_MESSAGES/linuxusbcreator.mo
%{_datadir}/linuxusbcreator/linux-compatibility.json
%{_mandir}/man1/linuxusbcreator.1*

%changelog
* Tue Jul 21 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.7.0-1
- Renew the graphical interface with an adaptive, task-oriented workflow
- Improve device states, destructive confirmation and operation feedback
- Preserve the existing raw, Windows and Fedora Live creation profiles

* Mon Jul 20 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.6.0-1
- Add extracted Fedora Live UEFI media with optional OverlayFS persistence
- Preserve bundled shim/GRUB and verify all extracted files
- Add compatibility fixtures and documented provenance

* Mon Jul 20 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.5.1-1
- Make UEFI/GPT Windows media visible in Windows and Linux file managers
- Keep removable-media UEFI boot files while using a Microsoft Basic Data partition
- Validate Windows/Linux visibility and x64 UEFI/BIOS boot in hardware

* Mon Jul 20 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.5.0-1
- Add UEFI/GPT and BIOS/MBR Windows media creation with per-stage progress
- Prevent UDisks target-mount authorization timeouts
- Clarify safe removal after Windows media verification
- Validate UEFI/GPT and BIOS/MBR x64 media on physical hardware

* Mon Jul 20 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.4.0-1
- Add the functional graphical image writing workflow

* Mon Jul 20 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.3.1-1
- Show all supported command-line modes in --help

* Mon Jul 20 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.3.0-1
- Add localization, manual, reproducible sources and package smoke tests

* Mon Jul 20 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.2.0-1
- Add verified raw image writing through a Polkit helper

* Mon Jul 20 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.1.0-1
- Initial read-only device discovery milestone
