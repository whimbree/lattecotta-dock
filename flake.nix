{
  # Phase 0 of docs/PORTING_PLAN.md: a reproducible Qt6/KF6 toolchain so the
  # port has something to configure against from the first commit. Phase 11
  # extends this with packages.default, overlays.default, nixosModules.default.
  description = "latte-dock Plasma 6/Qt6 port - development environment";

  # Pinned to the exact nixpkgs revision the development machine's NixOS runs
  # (26.11, Plasma 6.6.5 / Qt 6.11.0), so the dock links the same
  # libplasma/libtaskmanager/Qt as the live KWin compositor it is tested
  # against. A newer pin (e.g. nixos-unstable's Plasma 6.7.2) mismatches the
  # running compositor's plasma-window-management, which hands the tasks model
  # only a partial window list, and its Plasma theme plugin crashes the binary.
  # The final packaging (Phase 11) targets whatever the installing system has;
  # this pin is only the dev/test shell.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/567a49d1913ce81ac6e9582e3553dd90a955875f";

  outputs = { self, nixpkgs }:
    let
      # Development happens on x86_64 NixOS; widen when someone needs it.
      systems = [ "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems
        (system: f nixpkgs.legacyPackages.${system});
    in
    {
      packages = forAllSystems (pkgs: rec {
        latte-dock = pkgs.kdePackages.callPackage ./package.nix { };
        default = latte-dock;
      });

      # Lets other flakes pull the dock in with `overlays.default`, resolving
      # its Qt6/KF6 inputs from whatever nixpkgs the consumer runs (not this
      # flake's dev pin).
      overlays.default = final: prev: {
        latte-dock = final.kdePackages.callPackage ./package.nix { };
      };

      nixosModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.latte-dock;
        in
        {
          options.programs.latte-dock = {
            enable = lib.mkEnableOption "the latte-dock Plasma 6 port";
            package = lib.mkOption {
              type = lib.types.package;
              default = pkgs.kdePackages.callPackage ./package.nix { };
              defaultText = lib.literalExpression "pkgs.kdePackages.callPackage ./package.nix { }";
              description = "The latte-dock package to install.";
            };
          };

          config = lib.mkIf cfg.enable {
            # System-wide install so the .desktop lands in a path KWin's
            # KApplicationTrader scans (needed for the window-management gate)
            # and the KPackage plasmoids/shell/indicators are found.
            environment.systemPackages = [ cfg.package ];
          };
        };

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
              qt5compat # Qt5Compat.GraphicalEffects: ColorOverlay for the applet colorizer (flat color through alpha; MultiEffect.colorization is a luminance-preserving tint, not a substitute)
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

              # Applet PRIVATE QML modules (Phase 8): plasmoid packages load
              # from the session's XDG_DATA_DIRS, but their private plugins
              # must come from THIS pin - resolving them from the desktop's
              # differently-pinned tree is the shadowing regression the
              # comments above warn about. Same-pin whole-package roots are
              # safe: any module duplicated across these packages is the
              # identical derivation family, and the staged Latte modules
              # still win last.
              plasma-desktop # org.kde.private.desktopcontainment.folder, pager, kicker, kcm_keyboard
              bluedevil # org.kde.plasma.private.bluetooth
              bluez-qt # org.kde.bluezqt (bluetooth applet's second import)
              plasma-nm # org.kde.plasma.networkmanagement
              kdeconnect-kde # org.kde.kdeconnect
              kdeplasma-addons # org.kde.plasma.private.dict/comic/...
              qtwebengine # QtWebEngine for the dictionary applet
              powerdevil # org.kde.plasma.private.batterymonitor/brightnesscontrolplugin
              print-manager # org.kde.plasma.printmanager
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
