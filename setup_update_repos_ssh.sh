#!/usr/bin/env bash
# Clones of the update repos in UpdateRepos: origin fetches from gitflic and
# pushes to gitflic + gitlab + github + gitverse at once. Safe to re-run: existing
# clones are kept, only missing mirrors and fetch rules are added (no duplicates).
set -e

mkdir -p UpdateRepos
cd UpdateRepos

# repo <folder> <gitflic> <gitlab> <github> [gitverse]
repo() {
  [ -d "$1/.git" ] || git clone "$2" "$1"
  local url
  for url in "$3" "$4" "$5"; do
    [ -n "$url" ] || continue
    git -C "$1" config --get-all --fixed-value remote.origin.url "$url" >/dev/null ||
      git -C "$1" remote set-url --add origin "$url"
  done
}

# launcher content (main/test)
repo launcher-pwc git@gitflic.ru:prime-world-classic/launcher-pwc.git git@gitlab.com:prime-world-classic/launcher-pwc.git git@github.com:Prime-World-Classic/launcher-pwc.git git@gitverse.ru:prime_world_classic/launcher-pwc.git
# game client updates (main/test)
repo game-pwc git@gitflic.ru:prime-world-classic/game-pwc.git git@gitlab.com:prime-world-classic/game-pwc.git git@github.com:Prime-World-Classic/game-pwc.git git@gitverse.ru:prime_world_classic/game-pwc.git
# updater in Tools (main/test)
repo updater git@gitflic.ru:prime-world-classic/updater.git git@gitlab.com:prime-world-classic/updater.git git@github.com:Prime-World-Classic/updater.git git@gitverse.ru:prime_world_classic/updater.git
