{
  # Phase 0 of docs/PORTING_PLAN.md: a reproducible Qt6/KF6 toolchain so the
  # port has something to configure against from the first commit. Phase 11
  # extends this with packages.default, overlays.default, nixosModules.default.
  description = "latte-dock Plasma 6/Qt6 port - development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      # Development happens on x86_64 NixOS; widen when someone needs it.
      systems = [ "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems
        (system: f nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            kdePackages.extra-cmake-modules
          ];

          # The QML module search path for the headless QML checks and the
          # staged live runs (scripts/lib-qml-env.sh). mkShell does not run
          # the Qt env hooks, so nothing else exports these paths - and the
          # desktop session leaks its own (differently pinned) Plasma paths
          # into the environment, which must never win. Derived from the
          # dependency list below at eval time; entries without a qml dir
          # are filtered out by the consumer.
          LATTE_QML_MODULE_PATH = pkgs.lib.makeSearchPath "lib/qt-6/qml"
            (with pkgs.kdePackages; [
              qtdeclarative
              qtwayland
              libplasma
              plasma-activities
              plasma-workspace
              plasma-pa
              plasma5support
              kpipewire
              libksysguard
              kirigami
              ksvg
              kdeclarative
              kwindowsystem
              knotifications
              kcmutils
              knewstuff
              kitemmodels
              kconfig
              kcoreaddons
              ki18n
              kiconthemes
              qqc2-desktop-style
              kio
              solid
              sonnet
              ktextwidgets
            ]);

          buildInputs = (with pkgs.kdePackages; [
            # Qt
            qtbase
            qtdeclarative
            qtwayland

            # Plasma (de-umbrella'd from KF5 Plasma/PlasmaQuick, see Phase 1/3)
            libplasma
            plasma-activities
            plasma-activities-stats
            plasma-workspace # LibTaskManager, LibNotificationManager
            plasma-pa # org.kde.plasma.private.volume for the tasks audio badge
            kpipewire # org.kde.pipewire for wayland window thumbnails
            plasma5support # org.kde.plasma.plasma5support for the tasks plasmoid
            libksysguard
            kwayland
            plasma-wayland-protocols
            layer-shell-qt

            # KDE Frameworks 6
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
            kwindowsystem # includes KX11Extras for the best-effort X11 path
            kxmlgui
          ]) ++ (with pkgs; [
            wayland

            # Best-effort X11 path (HAVE_X11): XCB RANDR/SHAPE/EVENT + SM per
            # the top-level CMakeLists. Qt5X11Extras is gone in Qt6; native
            # handles come from QNativeInterface::QX11Application instead.
            libx11
            libsm
            libice
            libxcb
            libxcb-util
            libxrandr
          ]);

          # Building works from this shell as-is. Running the built binary
          # needs the usual Qt env wrapping (plugin/QML paths); revisit when
          # the first runnable milestone lands (end of Phase 5).
        };
      });
    };
}
