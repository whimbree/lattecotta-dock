Installation
============

## Dependencies

The port builds against Qt 6 and KDE Frameworks 6. The authoritative
per-distro dependency lists live with the native packages under
`packaging/` (arch, debian, fedora/rpm, gentoo, void) and in the distro
CI container definitions under `ci/containers/`; on Nix the flake
pins everything. The Qt5-era package lists this file used to carry
predate the port and were removed with the legacy install script.

### Building and Installing

Build and install with CMake directly (the legacy install.sh wrapper was
removed; it only wrapped these commands):

```
cmake -B build -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

On Nix, `nix build` / the flake's package output replaces this flow
entirely, and native distro packages live under `packaging/`.

