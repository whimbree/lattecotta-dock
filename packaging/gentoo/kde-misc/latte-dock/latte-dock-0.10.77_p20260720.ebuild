# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-only

EAPI=8

ECM_DEBUG=false
ECM_TEST=true
# cmake.eclass clears Release's built-in flags, so Gentoo CFLAGS and LDFLAGS
# remain authoritative while Latte still enables its production Qt semantics.
CMAKE_BUILD_TYPE=Release
KFMIN=6.5.0
inherit ecm

DESCRIPTION="Dock-style launcher and task manager for Plasma 6"
HOMEPAGE="https://github.com/whimbree/lattecotta-dock"

COMMIT="80451925475d7d5b0fb6d74f6b43d782c81dc4b5"
SRC_URI="https://github.com/whimbree/lattecotta-dock/archive/${COMMIT}.tar.gz -> ${P}.tar.gz"
S="${WORKDIR}/lattecotta-dock-${COMMIT}"

LICENSE="CC0-1.0 GPL-2+ LGPL-2+ LGPL-2.1 || ( LGPL-2.1 LGPL-3 )"
SLOT="0"
KEYWORDS="~amd64"

COMMON_DEPEND="
	>=dev-libs/wayland-1.15
	>=dev-qt/qtbase-6.9.0:6=[dbus,gui,wayland,widgets]
	>=dev-qt/qtdeclarative-6.9.0:6[widgets]
	>=dev-qt/qtwayland-6.9.0:6
	>=kde-frameworks/karchive-${KFMIN}:6
	>=kde-frameworks/kconfig-${KFMIN}:6[qml]
	>=kde-frameworks/kcoreaddons-${KFMIN}:6
	>=kde-frameworks/kcrash-${KFMIN}:6
	>=kde-frameworks/kdbusaddons-${KFMIN}:6
	>=kde-frameworks/kglobalaccel-${KFMIN}:6
	>=kde-frameworks/kguiaddons-${KFMIN}:6
	>=kde-frameworks/ki18n-${KFMIN}:6
	>=kde-frameworks/kiconthemes-${KFMIN}:6
	>=kde-frameworks/kio-${KFMIN}:6
	>=kde-frameworks/kirigami-${KFMIN}:6
	>=kde-frameworks/kitemmodels-${KFMIN}:6
	>=kde-frameworks/kjobwidgets-${KFMIN}:6
	>=kde-frameworks/knewstuff-${KFMIN}:6
	>=kde-frameworks/knotifications-${KFMIN}:6
	>=kde-frameworks/kpackage-${KFMIN}:6
	>=kde-frameworks/kservice-${KFMIN}:6
	>=kde-frameworks/ksvg-${KFMIN}:6
	>=kde-frameworks/kwidgetsaddons-${KFMIN}:6
	>=kde-frameworks/kwindowsystem-${KFMIN}:6[wayland]
	>=kde-frameworks/kxmlgui-${KFMIN}:6
	>=kde-plasma/kwayland-6.5.0:6
	>=kde-plasma/layer-shell-qt-6.5.0:6
	>=kde-plasma/libksysguard-6.5.0:6
	>=kde-plasma/libplasma-6.5.0:6=
	>=kde-plasma/plasma-activities-6.5.0:6=
	>=kde-plasma/plasma-activities-stats-6.5.0:6
	>=kde-plasma/plasma-workspace-6.5.0:6
"
DEPEND="${COMMON_DEPEND}
	>=dev-libs/plasma-wayland-protocols-1.6
	test? (
		>=dev-qt/qtbase-6.9.0:6=[vulkan]
		>=dev-qt/qtdeclarative-6.9.0:6=[vulkan]
		dev-util/vulkan-headers
		media-libs/vulkan-loader
	)
"
RDEPEND="${COMMON_DEPEND}
	>=dev-qt/qt5compat-6.9.0:6[qml]
	>=kde-frameworks/kdeclarative-${KFMIN}:6
	>=kde-frameworks/qqc2-desktop-style-${KFMIN}:6
	>=kde-plasma/kpipewire-6.5.0:6
	>=kde-plasma/kwin-6.5.0:6
	>=kde-plasma/plasma-pa-6.5.0:6
"
BDEPEND="
	dev-util/wayland-scanner
	sys-devel/gettext
	virtual/pkgconfig
	test? (
		>=dev-qt/qtdeclarative-6.9.0:6
		net-misc/rsync
	)
"

# The warning baseline is pinned to the Nix toolchain. schemesmodeltest also
# sees Gentoo's system color schemes despite its isolated XDG fixture. Portage
# tests upgrades before replacing the installed Latte QML plugin, which
# collides with themeawareicontest's direct registration of the same module.
CMAKE_SKIP_TESTS=(
	qmllintgate
	schemesmodeltest
	themeawareicontest
)

DOCS=(
	README.md
	packaging/ATTRIBUTION.md
)

src_test() {
	local qml_root="${EPREFIX}/usr/$(get_libdir)/qt6/qml"
	local qt_tools="${BROOT}/usr/$(get_libdir)/qt6/bin"
	local -x LATTE_QML_MODULE_PATH="${qml_root}"
	local -x PATH="${qt_tools}:${PATH}"
	local -x QT_QPA_PLATFORMTHEME=""
	unset NIXPKGS_QML_SEARCH_PATHS NIXPKGS_QT6_QML_IMPORT_PATH
	unset QML2_IMPORT_PATH QML_IMPORT_PATH QT_PLUGIN_PATH

	ecm_src_test
}
