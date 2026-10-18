#!/usr/bin/env bash
# Clones of the update repos in UpdateRepos: origin fetches from gitflic and
# pushes to gitflic + gitlab + github at once. Safe to re-run: existing clones
# are kept, only missing mirrors and fetch rules are added (no duplicates).
set -e

mkdir -p UpdateRepos
cd UpdateRepos

# repo <folder> <gitflic> <gitlab> <github> [testing]
repo() {
  [ -d "$1/.git" ] || git clone "$2" "$1"
  local url
  for url in "$3" "$4"; do
    git -C "$1" config --get-all --fixed-value remote.origin.url "$url" >/dev/null ||
      git -C "$1" remote set-url --add origin "$url"
  done
  # Hidden testers ref (refs/testing/test) is not fetched by default
  if [ -n "$5" ]; then
    git -C "$1" config --get-all --fixed-value remote.origin.fetch '+refs/testing/*:refs/remotes/origin/testing/*' >/dev/null ||
      git -C "$1" config --add remote.origin.fetch '+refs/testing/*:refs/remotes/origin/testing/*'
  fi
}

# production launcher (+ testers in refs/testing/test)
repo content_pwc https://gitflic.ru/project/prime-world-classic/content_pwc.git https://gitlab.com/prime-world-classic/content.git https://github.com/Prime-World-Classic/content.git testing
# test launcher (old scheme)
repo content-test https://gitflic.ru/project/prime-world-classic/content-test.git https://gitlab.com/prime-world-classic/content-test.git https://github.com/Prime-World-Classic/content-test.git
# production client (+ testers in refs/testing/test)
repo pwc-gitupdates https://gitflic.ru/project/prime-world-classic/pwc-gitupdates.git https://gitlab.com/prime-world-classic/PWCGitUpdates.git https://github.com/Prime-World-Classic/PWCGitUpdates.git testing
# test client (old scheme)
repo pwc-gitupdates-test https://gitflic.ru/project/prime-world-classic/pwc-gitupdates-test.git https://gitlab.com/prime-world-classic/pwc-git-updates-test.git https://github.com/Prime-World-Classic/PWCGitUpdates-test.git
