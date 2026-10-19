#!/usr/bin/env bash
# Clones of the update repos in UpdateRepos: origin fetches from gitflic and
# pushes to gitflic + gitlab + github at once. Safe to re-run: existing clones
# are kept, only missing mirrors and fetch rules are added (no duplicates).
set -e

mkdir -p UpdateRepos
cd UpdateRepos

# repo <folder> <gitflic> <gitlab> <github>
repo() {
  [ -d "$1/.git" ] || git clone "$2" "$1"
  local url
  for url in "$3" "$4"; do
    git -C "$1" config --get-all --fixed-value remote.origin.url "$url" >/dev/null ||
      git -C "$1" remote set-url --add origin "$url"
  done
}

# launcher content (main/test)
repo launcher-pwc git@gitflic.ru:prime-world-classic/launcher-pwc.git git@gitlab.com:prime-world-classic/launcher-pwc.git git@github.com:Prime-World-Classic/launcher-pwc.git
# game client updates (main/test)
repo game-pwc git@gitflic.ru:prime-world-classic/game-pwc.git git@gitlab.com:prime-world-classic/game-pwc.git git@github.com:Prime-World-Classic/game-pwc.git
# updater in Tools (main/test)
repo updater git@gitflic.ru:prime-world-classic/updater.git git@gitlab.com:prime-world-classic/updater.git git@github.com:Prime-World-Classic/updater.git
