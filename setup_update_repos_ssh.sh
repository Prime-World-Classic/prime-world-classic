#!/usr/bin/env bash
# Clones of the update repos in UpdateRepos: origin fetches from gitverse and
# pushes to gitflic + gitlab + github + gitverse at once (game-pwc: no gitflic,
# it is a retired mirror there). Safe to re-run: existing clones are kept,
# missing push mirrors are added (no duplicates) and fetch is moved to gitverse.
set -e

mkdir -p UpdateRepos
cd UpdateRepos

# repo <folder> <gitflic> <gitlab> <github> <gitverse>
# All of them go to remote.origin.pushurl (git then pushes only there), fetch url
# is gitverse; "-" skips a mirror. Push urls are added before the fetch url is
# replaced, so a clone never loses a mirror on the way.
repo() {
  [ -d "$1/.git" ] || git clone "$5" "$1"
  local url
  for url in "$2" "$3" "$4" "$5"; do
    [ "$url" = - ] && continue
    git -C "$1" config --get-all --fixed-value remote.origin.pushurl "$url" >/dev/null ||
      git -C "$1" remote set-url --add --push origin "$url"
  done
  git -C "$1" config --replace-all remote.origin.url "$5"
}

# drop <folder> <url> - remove a retired mirror from the push urls
drop() {
  git -C "$1" config --get-all --fixed-value remote.origin.pushurl "$2" >/dev/null || return 0
  git -C "$1" config --unset --fixed-value remote.origin.pushurl "$2"
}

# launcher content (main/test)
repo launcher-pwc git@gitflic.ru:prime-world-classic/launcher-pwc.git git@gitlab.com:prime-world-classic/launcher-pwc.git git@github.com:Prime-World-Classic/launcher-pwc.git git@gitverse.ru:prime_world_classic/launcher-pwc.git
# game client updates (main/test)
repo game-pwc - git@gitlab.com:prime-world-classic/game-pwc.git git@github.com:Prime-World-Classic/game-pwc.git git@gitverse.ru:prime_world_classic/game-pwc.git
drop game-pwc git@gitflic.ru:prime-world-classic/game-pwc.git
# updater in Tools (main/test)
repo updater git@gitflic.ru:prime-world-classic/updater.git git@gitlab.com:prime-world-classic/updater.git git@github.com:Prime-World-Classic/updater.git git@gitverse.ru:prime_world_classic/updater.git
