# SPDX-FileCopyrightText: 2026 x3ro
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cask for the x3ro/homebrew-tap repository (Casks/x3ro-deck.rb). The release
# workflow updates version and sha256.
cask "x3ro-deck" do
  version "0.1.0"
  sha256 :no_check

  url "https://github.com/x3ro/x3ro-deck/releases/download/v#{version}/x3ro-deck-#{version}.dmg"
  name "x3ro deck"
  desc "Desktop cockpit for coding agents and their Docker Compose stacks"
  homepage "https://github.com/x3ro/x3ro-deck"

  depends_on macos: ">= :monterey"

  app "x3ro-deck.app"
  binary "#{appdir}/x3ro-deck.app/Contents/MacOS/x3ro-deck-hook"
  binary "#{appdir}/x3ro-deck.app/Contents/MacOS/x3ro-deck-attach"

  zap trash: [
    "~/Library/Preferences/x3ro-deck",
    "~/Library/Caches/x3ro-deck",
  ]
end
