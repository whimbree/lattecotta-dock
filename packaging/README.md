# Native packaging recipes

Local native package recipes for lattecotta-dock / latte-dock. These recipes
cover all five native formats and build installable artifacts from the source
tree. No official package or repository, package publication, package-artifact
CI, release, tag, artifact upload, sponsorship, or distribution endorsement
exists.

## Layout

- `debian/` - Debian/Ubuntu-family `.deb` package and `build-package` helper
- `rpm/` - shared Fedora/openSUSE `.rpm` spec and snapshot-source helper
- `arch/` - Arch Linux `PKGBUILD` and generated `.SRCINFO`
- `gentoo/` - Gentoo EAPI 8 local-overlay ebuild and metadata
- `void/` - Void `xbps-src` template and clean-source staging helper
- `ATTRIBUTION.md` - consolidated copyright lines gathered from SPDX headers

## Gate discipline

The Debian-family, shared RPM, Arch, and Void recipes have fresh-environment
install evidence. The Gentoo recipe produced a GPKG and was exercised through a
Portage package/source reinstall, yielding a Portage-owned manifest and
installed-artifact runtime proof. Across the five formats, the shared gate
verified the installed executable, plugins, data, nested-KWin startup, artifact
mappings, and clean shutdown. Package-artifact CI remains pending outside the
completed local-recipe scope.
