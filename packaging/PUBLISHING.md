# Making the repository public

The private history names internal tools and machines. The public repository
gets a rewritten copy; the private one is archived as it is.

1. Install git-filter-repo (`pip install git-filter-repo` or the distro
   package) and make sure `.git/info/public-history-replacements` holds the
   replacements (kept out of the tree on purpose).
2. `scripts/prepare-public-history.sh ../x3ro-deck-public`. It clones,
   rewrites files, paths and messages, sets every author to `x3ro
   <noreply@x3ro.life>` (`--keep-authors` keeps them), and fails if the scan
   finds anything.
3. Check it builds from scratch:
   `cd ../x3ro-deck-public && cmake --preset linux && cmake --build --preset linux && ctest --test-dir build`.
4. Create a **new, empty** GitHub repository (never reuse the private remote's
   refs), then from the rewritten copy:
   `git remote add origin git@github.com:djhype/x3ro-deck.git && git push -u origin main --tags`.
5. Wait for CI to pass, then make the repository public and archive the
   private original (Settings → Archive), keeping it.
6. Continue with the release checklist: secrets and the `release` environment
   (`.github/workflows/release.yml`), the AUR, OBS, the Homebrew tap and winget
   (`packaging/README.md`).
