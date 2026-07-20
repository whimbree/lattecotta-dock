# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Derived from this repository's RPM prototypes at
# 9522d4b094e81dfca0bfe28902ad2aae3d359801 and
# 821beb8e9abd1f5d6fee6d089c2a15a9cab22016. This version uses the
# installed-package gate introduced after those prototypes.
#
# This is an untagged snapshot recipe. make-snapshot-source.sh creates Source0
# from an unchanged tracked HEAD and a build spec with the commit and its
# YYYYMMDD date defined. Use that generated spec so both the binary and source
# packages remain bound to the exact tree without external rebuild arguments.
%{!?snapshot_commit:%{error:Define snapshot_commit as the exact 40-hex source commit}}
%{!?snapshot_date:%{error:Define snapshot_date as the source commit date in YYYYMMDD form}}

Name:           latte-dock
Version:        0.10.77
Release:        0.1.%{snapshot_date}git%{snapshot_commit}%{?dist}
Summary:        Dock and task launcher for Plasma 6

License:        GPL-2.0-or-later AND LGPL-2.0-or-later AND (LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL) AND CC0-1.0
URL:            https://github.com/whimbree/lattecotta-dock
Source0:        %{name}-%{version}-%{snapshot_commit}.tar.gz
Patch0:         latte-dock-appstream-id.patch

# Fedora and openSUSE package the same Qt 6 and Plasma 6 interfaces under
# different names. Keep every name difference here; the dependency lists and
# build phases below remain shared.
%if 0%{?suse_version}
# Tumbleweed scans every packaged QML file. These modules are registered from
# the Latte process or Plasma package resources and therefore have no qmldir
# capability for RPM to discover. Their owning runtime packages remain explicit
# below; all normal QML dependency generation stays enabled.
%global __requires_exclude ^qt6qmlimport[(]org[.]kde[.](latte[.]private[.]app|taskmanager|plasma[.]private[.](mpris|shell|volume))
%global cmake_generator_pkg          ninja
%global cmake_macros_pkg             kf6-extra-cmake-modules
%global gettext_pkg                 gettext-tools
%global appstream_validator_pkg     AppStream
%global ecm_pkg                     kf6-extra-cmake-modules
%global qtbase_devel_pkg            qt6-base-devel
%global qtdeclarative_devel_pkg     qt6-declarative-devel
%global qtwayland_devel_pkg         qt6-wayland-devel
%global libplasma_devel_pkg         libplasma6-devel
%global plasma_activities_devel_pkg plasma6-activities-devel
%global plasma_stats_devel_pkg      plasma6-activities-stats-devel
%global plasma_workspace_devel_pkg  plasma6-workspace-devel
%global kwayland_devel_pkg          kwayland6-devel
%global layershell_devel_pkg        layer-shell-qt6-devel
%global libksysguard_devel_pkg      libksysguard6-devel
%global plasma_wayland_protocols_pkg plasma-wayland-protocols
%global qtdeclarative_runtime_pkg   qt6-declarative-imports
%global qt5compat_runtime_pkg       qt6-qt5compat-imports
%global kdeclarative_runtime_pkg    kf6-kdeclarative-imports
%global kirigami_runtime_pkg        kf6-kirigami-imports
%global ksvg_runtime_pkg            kf6-ksvg-imports
%global kwindowsystem_runtime_pkg   kf6-kwindowsystem-imports
%global libplasma_runtime_pkg       libplasma6-components
%global plasma_activities_runtime_pkg plasma6-activities-imports
%global plasma_workspace_runtime_pkg plasma6-workspace
%global kpipewire_runtime_pkg       kpipewire6-imports
%global plasma_pa_runtime_pkg       plasma6-pa
%global layershell_runtime_pkg      layer-shell-qt6
%global kwin_runtime_pkg            kwin6
%else
%global cmake_generator_pkg          ninja-build
%global cmake_macros_pkg             cmake-rpm-macros
%global gettext_pkg                 gettext
%global appstream_validator_pkg     appstream
%global ecm_pkg                     extra-cmake-modules
%global qtbase_devel_pkg            qt6-qtbase-devel
%global qtdeclarative_devel_pkg     qt6-qtdeclarative-devel
%global qtwayland_devel_pkg         qt6-qtwayland-devel
%global libplasma_devel_pkg         libplasma-devel
%global plasma_activities_devel_pkg plasma-activities-devel
%global plasma_stats_devel_pkg      plasma-activities-stats-devel
%global plasma_workspace_devel_pkg  plasma-workspace-devel
%global kwayland_devel_pkg          kwayland-devel
%global layershell_devel_pkg        layer-shell-qt-devel
%global libksysguard_devel_pkg      libksysguard-devel
%global plasma_wayland_protocols_pkg plasma-wayland-protocols-devel
%global qtdeclarative_runtime_pkg   qt6-qtdeclarative
%global qt5compat_runtime_pkg       qt6-qt5compat
%global kdeclarative_runtime_pkg    kf6-kdeclarative
%global kirigami_runtime_pkg        kf6-kirigami
%global ksvg_runtime_pkg            kf6-ksvg
%global kwindowsystem_runtime_pkg   kf6-kwindowsystem
%global libplasma_runtime_pkg       libplasma
%global plasma_activities_runtime_pkg plasma-activities
%global plasma_workspace_runtime_pkg plasma-workspace
%global kpipewire_runtime_pkg       kpipewire
%global plasma_pa_runtime_pkg       plasma-pa
%global layershell_runtime_pkg      layer-shell-qt
%global kwin_runtime_pkg            kwin
%endif

BuildRequires:  cmake >= 3.16
BuildRequires:  %{cmake_generator_pkg}
BuildRequires:  %{cmake_macros_pkg}
BuildRequires:  gcc-c++
BuildRequires:  %{gettext_pkg}
BuildRequires:  patch
BuildRequires:  pkgconf-pkg-config
BuildRequires:  %{appstream_validator_pkg}
BuildRequires:  desktop-file-utils
BuildRequires:  %{ecm_pkg} >= 6.5

BuildRequires:  %{qtbase_devel_pkg} >= 6.6
BuildRequires:  %{qtdeclarative_devel_pkg} >= 6.6
BuildRequires:  %{qtwayland_devel_pkg} >= 6.6

BuildRequires:  kf6-karchive-devel >= 6.5
BuildRequires:  kf6-kconfig-devel >= 6.5
BuildRequires:  kf6-kcoreaddons-devel >= 6.5
BuildRequires:  kf6-kcrash-devel >= 6.5
BuildRequires:  kf6-kdbusaddons-devel >= 6.5
BuildRequires:  kf6-kdeclarative-devel >= 6.5
BuildRequires:  kf6-kglobalaccel-devel >= 6.5
BuildRequires:  kf6-kguiaddons-devel >= 6.5
BuildRequires:  kf6-ki18n-devel >= 6.5
BuildRequires:  kf6-kiconthemes-devel >= 6.5
BuildRequires:  kf6-kitemmodels-devel >= 6.5
BuildRequires:  kf6-kio-devel >= 6.5
BuildRequires:  kf6-kirigami-devel >= 6.5
BuildRequires:  kf6-knewstuff-devel >= 6.5
BuildRequires:  kf6-knotifications-devel >= 6.5
BuildRequires:  kf6-kpackage-devel >= 6.5
BuildRequires:  kf6-kservice-devel >= 6.5
BuildRequires:  kf6-ksvg-devel >= 6.5
BuildRequires:  kf6-kwindowsystem-devel >= 6.5
BuildRequires:  kf6-kxmlgui-devel >= 6.5

BuildRequires:  %{libplasma_devel_pkg} >= 6.5
BuildRequires:  %{plasma_activities_devel_pkg} >= 6.5
BuildRequires:  %{plasma_stats_devel_pkg} >= 6.5
BuildRequires:  %{plasma_workspace_devel_pkg} >= 6.5
BuildRequires:  %{kwayland_devel_pkg} >= 6.5
BuildRequires:  %{layershell_devel_pkg} >= 6.5
BuildRequires:  %{libksysguard_devel_pkg} >= 6.5
BuildRequires:  %{plasma_wayland_protocols_pkg}
BuildRequires:  wayland-devel

# Shared-library requirements are generated by RPM. These explicit runtime
# requirements cover QML imports and the Wayland compositor, which ELF
# dependency generation cannot discover from packaged QML.
Requires:       %{qtdeclarative_runtime_pkg} >= 6.6
Requires:       %{qt5compat_runtime_pkg} >= 6.6
Requires:       %{kdeclarative_runtime_pkg} >= 6.5
Requires:       %{kirigami_runtime_pkg} >= 6.5
Requires:       %{ksvg_runtime_pkg} >= 6.5
Requires:       %{kwindowsystem_runtime_pkg} >= 6.5
Requires:       kf6-qqc2-desktop-style >= 6.5
Requires:       %{libplasma_runtime_pkg} >= 6.5
Requires:       %{plasma_activities_runtime_pkg} >= 6.5
Requires:       %{plasma_workspace_runtime_pkg} >= 6.5
Requires:       %{kpipewire_runtime_pkg} >= 6.5
Requires:       %{plasma_pa_runtime_pkg} >= 6.5
Requires:       %{layershell_runtime_pkg} >= 6.5
Requires:       %{kwin_runtime_pkg} >= 6.5

%description
Latte Dock is maintained for Plasma 6 and Qt 6 while retaining the established
latte-dock executable, D-Bus, and package identifiers. It provides task
launchers, Plasma widgets, multiple layouts, and a Wayland layer-shell dock
surface.

%prep
%autosetup -p1 -n %{name}-%{version}

%build
# The native macros supply each distro's compiler and linker hardening flags.
%if 0%{?suse_version}
# Tumbleweed's KF6 macro does not enable PIE for executables at suse_version
# 1699, so supply the compile and link halves explicitly.
%cmake_kf6 \
    -DBUILD_TESTING=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_EXE_LINKER_FLAGS:STRING="%{?build_ldflags} -Wl,--as-needed -Wl,--no-undefined -pie"
%ninja_build -C build
%else
%cmake -GNinja -DBUILD_TESTING=OFF
%cmake_build
%endif

%install
%if 0%{?suse_version}
%ninja_install -C build
%else
%cmake_install
%endif
%find_lang %{name} --all-name

%check
test -x %{buildroot}%{_bindir}/latte-dock
test -f %{buildroot}%{_libdir}/qt6/qml/org/kde/latte/core/qmldir
test -f %{buildroot}%{_libdir}/qt6/qml/org/kde/latte/core/liblattecoreplugin.so
test -f %{buildroot}%{_libdir}/qt6/qml/org/kde/latte/private/containment/liblattecontainmentplugin.so
test -f %{buildroot}%{_libdir}/qt6/qml/org/kde/latte/private/tasks/liblattetasksplugin.so
test -f %{buildroot}%{_libdir}/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so
test -f %{buildroot}%{_libdir}/qt6/plugins/kpackage/packagestructure/latte_indicator.so
desktop-file-validate %{buildroot}%{_datadir}/applications/org.kde.latte-dock.desktop
appstreamcli validate --no-net \
    %{buildroot}%{_datadir}/metainfo/org.kde.latte-dock.appdata.xml

%files -f %{name}.lang
%license LICENSES/* packaging/ATTRIBUTION.md
%doc README.md
%{_bindir}/latte-dock
%{_libdir}/qt6/qml/org/kde/latte/
%{_libdir}/qt6/plugins/plasma/containmentactions/org.kde.latte.contextmenu.so
%{_libdir}/qt6/plugins/kpackage/packagestructure/latte_indicator.so
%{_datadir}/applications/org.kde.latte-dock.desktop
%{_datadir}/metainfo/org.kde.latte-dock.appdata.xml
%{_datadir}/dbus-1/interfaces/org.kde.LatteDock.xml
%{_datadir}/knotifications6/lattedock.notifyrc
%{_datadir}/knsrcfiles/latte-layouts.knsrc
%{_datadir}/knsrcfiles/latte-indicators.knsrc
%{_datadir}/kservicetypes6/latte-indicator.desktop
%{_datadir}/latte/
%{_datadir}/plasma/plasmoids/org.kde.latte.containment/
%{_datadir}/plasma/plasmoids/org.kde.latte.plasmoid/
%{_datadir}/plasma/shells/org.kde.latte.shell/
%{_datadir}/icons/hicolor/*/apps/latte-dock.svg
%{_datadir}/icons/breeze/applets/256/org.kde.latte.plasmoid.svg

%changelog
* Mon Jul 20 2026 Latte Dock contributors <20343235+whimbree@users.noreply.github.com> - 0.10.77-0.1
- Add a shared native RPM recipe for Fedora 43 and openSUSE Tumbleweed.
