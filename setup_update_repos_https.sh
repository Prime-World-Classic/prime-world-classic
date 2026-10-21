#!/usr/bin/env bash
# Clones of the update repos in UpdateRepos: origin fetches from gitverse and
# pushes to gitflic + gitlab + github + gitverse at once. Safe to re-run: existing
# clones are kept, missing push mirrors are added (no duplicates) and fetch is
# moved to gitverse.
set -e

mkdir -p UpdateRepos
cd UpdateRepos

# repo <folder> <gitflic> <gitlab> <github> <gitverse>
# All four go to remote.origin.pushurl (git then pushes only there), fetch url
# is gitverse. Push urls are added before the fetch url is replaced, so a clone
# never loses a mirror on the way.
repo() {
  [ -d "$1/.git" ] || git clone "$5" "$1"
  local url
  for url in "$2" "$3" "$4" "$5"; do
    git -C "$1" config --get-all --fixed-value remote.origin.pushurl "$url" >/dev/null ||
      git -C "$1" remote set-url --add --push origin "$url"
  done
  git -C "$1" config --replace-all remote.origin.url "$5"
}

# launcher content (main/test)
repo launcher-pwc https://gitflic.ru/project/prime-world-classic/launcher-pwc.git https://gitlab.com/prime-world-classic/launcher-pwc.git https://github.com/Prime-World-Classic/launcher-pwc.git https://gitverse.ru/prime_world_classic/launcher-pwc.git
# game client updates (main/test)
repo game-pwc https://gitflic.ru/project/prime-world-classic/game-pwc.git https://gitlab.com/prime-world-classic/game-pwc.git https://github.com/Prime-World-Classic/game-pwc.git https://gitverse.ru/prime_world_classic/game-pwc.git
# updater in Tools (main/test)
repo updater https://gitflic.ru/project/prime-world-classic/updater.git https://gitlab.com/prime-world-classic/updater.git https://github.com/Prime-World-Classic/updater.git https://gitverse.ru/prime_world_classic/updater.git
