#!/usr/bin/env bash
# Clones of the update repos in UpdateRepos: origin fetches from gitflic and
# pushes to gitflic + gitlab + github + gitverse at once. Safe to re-run: existing
# clones are kept, only missing mirrors and fetch rules are added (no duplicates).
set -e

mkdir -p UpdateRepos
cd UpdateRepos

# repo <folder> <gitflic> <gitlab> <github> [gitverse]
# Clones that use explicit pushurl entries (git pushes only to pushurl when they
# exist) get the mirror added as pushurl too.
repo() {
  [ -d "$1/.git" ] || git clone "$2" "$1"
  local url has_pushurl=0
  if git -C "$1" config --get-all remote.origin.pushurl >/dev/null 2>&1; then
    has_pushurl=1
  fi
  for url in "$3" "$4" "$5"; do
    [ -n "$url" ] || continue
    git -C "$1" config --get-all --fixed-value remote.origin.url "$url" >/dev/null ||
      git -C "$1" remote set-url --add origin "$url"
    if [ "$has_pushurl" = 1 ]; then
      git -C "$1" config --get-all --fixed-value remote.origin.pushurl "$url" >/dev/null ||
        git -C "$1" remote set-url --add --push origin "$url"
    fi
  done
}

# launcher content (main/test)
repo launcher-pwc https://gitflic.ru/project/prime-world-classic/launcher-pwc.git https://gitlab.com/prime-world-classic/launcher-pwc.git https://github.com/Prime-World-Classic/launcher-pwc.git https://gitverse.ru/prime_world_classic/launcher-pwc.git
# game client updates (main/test)
repo game-pwc https://gitflic.ru/project/prime-world-classic/game-pwc.git https://gitlab.com/prime-world-classic/game-pwc.git https://github.com/Prime-World-Classic/game-pwc.git https://gitverse.ru/prime_world_classic/game-pwc.git
# updater in Tools (main/test)
repo updater https://gitflic.ru/project/prime-world-classic/updater.git https://gitlab.com/prime-world-classic/updater.git https://github.com/Prime-World-Classic/updater.git https://gitverse.ru/prime_world_classic/updater.git
