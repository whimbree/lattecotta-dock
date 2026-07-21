{
  # Phase 0 of docs/tracking/PORTING_PLAN.md: a reproducible Qt6/KF6 toolchain so the
  # port has something to configure against from the first commit. Phase 11
  # extends this with packages.default, overlays.default, nixosModules.default.
  description = "latte-dock Plasma 6/Qt6 port - development environment";

  # Pinned to the exact nixpkgs revision the development machine's NixOS runs
  # (26.11.20260715, Plasma 6.7.3 / Qt 6.11.1), so the dock links the same
  # libplasma/libtaskmanager/Qt as the live KWin compositor it is tested
  # against. Any drift between this pin and the running system breaks the
  # kwin-QPA path for new nested compositors (the 2026-07-17 incident:
  # SIGSEGV at QApplication init in every new nested kwin_wayland), so
  # gate-all.sh carries a lockstep guard that compares this pin against
  # /run/current-system and fails fast with a re-pin message on mismatch.
  # Re-pin recipe: readlink /run/current-system for the short rev, the root
  # nixpkgs node of /persist/etc/nixos/flake.lock for the full one.
  # The final packaging (Phase 11) targets whatever the installing system has;
  # this pin is only the dev/test shell.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/753cc8a3a87467296ddd1fa93f0cc3e81120ee46";

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
            autostart = lib.mkEnableOption "starting latte-dock automatically at session login";
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

            # System-level autostart: the package's own launcher entry,
            # linked into /etc/xdg/autostart (the same file upstream Latte's
            # in-app "autostart" toggle copies to ~/.config/autostart - a
            # user-level file with the same name deliberately OVERRIDES this
            # one per XDG precedence, so the in-app toggle keeps working and
            # nothing ever starts twice).
            environment.etc."xdg/autostart/org.kde.latte-dock.desktop" =
              lib.mkIf cfg.autostart {
                source = "${cfg.package}/share/applications/org.kde.latte-dock.desktop";
              };
          };
        };

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            appstream # appstreamcli for the direct configured-metadata CTest
            kdePackages.extra-cmake-modules
            jq # scripts/qmllint-gate.sh parses qmllint --json with it
            imagemagick # cropping live-verification and docs screenshots

            # clangd for the editor/IDE code-intelligence flow (docs/reference/clangd-setup.md):
            # a contributor gets a working language server straight from `nix develop`,
            # no separate install. clang-tools ships only the tooling binaries
            # (clangd, clang-format, clang-tidy) - it carries NO cc/gcc/g++/clang++
            # driver, so it cannot shadow the pinned GCC stdenv the build uses.
            clang-tools

            # sceneprobe render gate (scripts/sceneprobe-gate.sh): a nested
            # throwaway compositor gives the probe a Vulkan-capable wayland
            # QPA. Process-level tool only - its QML/plugin trees are never
            # added to any *_PATH (the import-path doctrine below).
            kdePackages.kwin # kwin_wayland --virtual
          ];

          # Pure-CPU Vulkan pieces for the sceneprobe render gate, from the
          # flake pin so goldens are blessed against the exact Mesa and
          # validation layer CI runs (never /run/opengl-driver, which is
          # whatever the host system happens to run). Consumed by
          # tests/sceneprobe/run_in_kwin.sh.
          LATTE_VULKAN_LAVAPIPE_ICD =
            "${pkgs.mesa}/share/vulkan/icd.d/lvp_icd.x86_64.json";
          LATTE_VK_LAYER_PATH =
            "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";

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
            kwindowsystem
            kxmlgui
          ]) ++ (with pkgs; [
            wayland

            # tests/sceneprobe links the Vulkan loader directly
            # (find_package(Vulkan)); headers for vulkan/vulkan.h.
            vulkan-headers
            vulkan-loader
          ]);

          # Building works from this shell as-is. Running the built binary
          # needs the usual Qt env wrapping (plugin/QML paths); revisit when
          # the first runnable milestone lands (end of Phase 5).
        };
      });
    };
}
