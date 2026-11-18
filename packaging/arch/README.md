# AUR packages

- `x3ro-deck/PKGBUILD` builds from the release's source tarball.
- `x3ro-deck-bin/PKGBUILD` repackages the release workflow's Linux build.

For each release the workflow (`.github/workflows/release.yml`) fills in
`pkgver` and `sha256sums`. To publish, from a clone of each AUR repository:

```sh
cp ../x3ro-deck/packaging/arch/x3ro-deck/PKGBUILD .
makepkg --printsrcinfo > .SRCINFO
namcap PKGBUILD && makepkg -sf && namcap x3ro-deck-*.pkg.tar.zst
git add PKGBUILD .SRCINFO && git commit -m "Update to $pkgver" && git push
```

`scripts/test-arch-package.sh` builds the source package from the working tree
(a `git archive` tarball in place of the release download) to check the
PKGBUILD before a release.
