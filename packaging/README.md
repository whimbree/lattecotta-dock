# Native packaging recipes

Tier-1 packaging recipes for lattecotta-dock / latte-dock.
Each recipe builds an installable artifact for its distro format using the
per-distro container environments established in Phase A/B/C of the
multi-distro CI plan. CI then builds, installs, and gates the installed
package (not just the build tree).

## Layout

- `debian/` - Debian/Ubuntu-family .deb package (dh cmake)
- `rpm/` - Fedora/openSUSE .rpm package (shared .spec)
- `arch/` - Arch Linux PKGBUILD (+ .SRCINFO)
- `gentoo/` - Gentoo overlay ebuild
- `void/` - Void Linux xbps-src template
- `ATTRIBUTION.md` - consolidated copyright lines gathered from SPDX headers

## Gate discipline

Every recipe is exercised in its target distro container via podman. The
built package is installed inside a fresh container layer, and the headless
gate stack (build -> ctest -> e2e smoke -> sceneprobe) is run against the
installed binary. A recipe is not done until that gate is green.
