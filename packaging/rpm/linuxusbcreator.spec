Name:           linuxusbcreator
Version:        0.1.0
Release:        1%{?dist}
Summary:        Native Linux utility for creating bootable USB media
License:        GPL-3.0-or-later
URL:            https://github.com/Zsk1dr0W/linuxusbcreator
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  gcc
BuildRequires:  meson
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib
BuildRequires:  pkgconfig(gio-2.0) >= 2.72
BuildRequires:  pkgconfig(gtk4) >= 4.8
BuildRequires:  pkgconfig(libadwaita-1) >= 1.2
Requires:       udisks2

%description
Linux USB Creator is a native Linux utility for safely inspecting removable USB
devices and creating bootable media. Version 0.1.0 provides read-only discovery.

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
%doc README.md ROADMAP.md
%{_bindir}/linuxusbcreator
%{_datadir}/applications/io.github.zsk1dr0w.LinuxUsbCreator.desktop
%{_datadir}/metainfo/io.github.zsk1dr0w.LinuxUsbCreator.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/io.github.zsk1dr0w.LinuxUsbCreator.png

%changelog
* Mon Jul 20 2026 Víctor Díaz Gonzalez <106137683+Zsk1dr0W@users.noreply.github.com> - 0.1.0-1
- Initial read-only device discovery milestone

