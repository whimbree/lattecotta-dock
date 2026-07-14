# Package derivation for the latte-dock Plasma 6/Qt6 port.
# Kept separate from flake.nix so the flake's packages output and its overlay
# can share one definition. Build it against a kdePackages scope, e.g.
# `pkgs.kdePackages.callPackage ./package.nix {}`, so the Qt/KF6/Plasma inputs
# resolve to the matching Qt6 builds.
#
# wrapQtAppsHook is deliberate, not incidental: it makes $out/bin/latte-dock a
# makeBinaryWrapper that keeps argv[0] = $out/bin/latte-dock. KWin gates the
# org_kde_plasma_window_management protocol on the client's argv[0] matching an
# installed .desktop Exec, and the installed org.kde.latte-dock.desktop carries
# Exec=$out/bin/latte-dock. So the wrapped, installed dock enumerates windows;
# see docs/REVIEW_NOTES.md for the full gate write-up.
{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  extra-cmake-modules,
  wrapQtAppsHook,

  # Qt
  qtbase,
  qtdeclarative,
  qtwayland,

  # Plasma
  libplasma,
  plasma-activities,
  plasma-activities-stats,
  plasma-workspace,
  plasma-pa,
  kpipewire,
  plasma5support,
  libksysguard,
  kwayland,
  plasma-wayland-protocols,
  layer-shell-qt,

  # KDE Frameworks 6
  karchive,
  kcmutils,
  kconfig,
  kcoreaddons,
  kcrash,
  kdbusaddons,
  kdeclarative,
  kglobalaccel,
  kguiaddons,
  ki18n,
  kiconthemes,
  kio,
  kirigami,
  knewstuff,
  knotifications,
  kpackage,
  ksvg,
  kwindowsystem,
  kxmlgui,

  # Pulled in for QML runtime imports (wrapQtAppsHook adds their qml dirs to
  # the wrapper's QML2_IMPORT_PATH); mirror of flake.nix LATTE_QML_MODULE_PATH.
  kitemmodels,
  solid,
  sonnet,
  ktextwidgets,
  qqc2-desktop-style,
  qt5compat, # Qt5Compat.GraphicalEffects ColorOverlay (applet colorizer)

  wayland,

  # Best-effort X11 backend. Off by default: Wayland is the only runtime the
  # port targets, and dropping it keeps the closure lean. The C++ still
  # compiles with it on (option WITH_X11 in the top-level CMakeLists).
  withX11 ? false,
  libX11,
  libSM,
  libICE,
  libxcb,
  xcbutil,
  libXrandr,
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "latte-dock";
  version = "0.10.77";

  src = ./.;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    extra-cmake-modules
    wrapQtAppsHook
  ];

  buildInputs = [
    qtbase
    qtdeclarative
    qtwayland

    libplasma
    plasma-activities
    plasma-activities-stats
    plasma-workspace
    plasma-pa
    kpipewire
    plasma5support
    libksysguard
    kwayland
    plasma-wayland-protocols
    layer-shell-qt

    karchive
    kcmutils
    kconfig
    kcoreaddons
    kcrash
    kdbusaddons
    kdeclarative
    kglobalaccel
    kguiaddons
    ki18n
    kiconthemes
    kio
    kirigami
    knewstuff
    knotifications
    kpackage
    ksvg
    kwindowsystem
    kxmlgui

    kitemmodels
    solid
    sonnet
    ktextwidgets
    qqc2-desktop-style
    qt5compat

    wayland
  ]
  ++ lib.optionals withX11 [
    libX11
    libSM
    libICE
    libxcb
    xcbutil
    libXrandr
  ];

  cmakeFlags = [
    (lib.cmakeBool "WITH_X11" withX11)
  ];

  meta = {
    description = "Dock-style launcher/task manager, Plasma 6/Qt6 port of Latte Dock";
    homepage = "https://github.com/whimbree/latte-dock";
    license = lib.licenses.gpl2Plus;
    platforms = lib.platforms.linux;
    mainProgram = "latte-dock";
  };
})
