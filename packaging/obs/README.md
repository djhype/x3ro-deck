# openSUSE Build Service

OBS builds and signs the pacman, apt, dnf and zypper repositories from
`packaging/arch/x3ro-deck/PKGBUILD`, `packaging/debian/` and
`packaging/rpm/x3ro-deck.spec`; each repository uses the recipe for its
distribution.

## Project setup (once)

1. Create the project `home:x3ro:deck` (or an `x3ro` project) on
   build.opensuse.org and the package `x3ro-deck` in it.
2. Add repositories: `xUbuntu_24.04`, `xUbuntu_24.10`, `Debian_12`,
   `Debian_13`, `Fedora_41`, `Fedora_42`, `openSUSE_Tumbleweed`
   (x86_64 and aarch64), and Arch Linux ("Arch Extra", x86_64) under the
   repository name `Arch`: `install.sh` expects
   `<project>/Arch/x86_64/home_x3ro_deck_Arch.key` and the pacman
   repository `[home_x3ro_deck_Arch]`.
3. Copy `_service` from this folder into the package. It builds the
   `release` branch, which the release workflow moves to each new tag (not
   pre-releases) before it asks OBS to run the service, so a release needs no
   change here.
4. OBS signs every repository with the project's key. Publish the key's
   fingerprint in the README; `install.sh` fetches the key from
   `https://download.opensuse.org/repositories/<project>/<distro>/Release.key`
   (Debian/Ubuntu), `repodata/repomd.xml.key` (RPM) or
   `Arch/x86_64/home_x3ro_deck_Arch.key` (Arch).

## Checking a build locally

`osc build xUbuntu_24.04 x86_64` or `osc build Fedora_42 x86_64` from a
checkout of the OBS package builds it in a clean chroot.
`scripts/test-deb-package.sh` builds the .deb from the working tree in an
Ubuntu 24.04 container.
