# Native packaging recipes

Local native package recipes for lattecotta-dock / latte-dock. These recipes
build installable artifacts from the source tree, but no package has been
uploaded or published through a distribution repository or package service.

## Layout

- `debian/` - Debian/Ubuntu-family `.deb` package and `build-package` helper
- `rpm/` - shared Fedora/openSUSE `.rpm` spec and snapshot-source helper
- `arch/` - Arch Linux `PKGBUILD` and generated `.SRCINFO`
- `ATTRIBUTION.md` - consolidated copyright lines gathered from SPDX headers

## Gate discipline

Each available recipe has produced a local package that was installed in a
fresh target environment. The package-manager ownership manifest and shared
installed-package gate then verify the installed executable, plugins, data,
nested-KWin startup, artifact mappings, and clean shutdown. Hosted CI remains
pending, as do the Gentoo ebuild and Void `xbps-src` template.
