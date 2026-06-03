#!/usr/bin/env bash
set -e

mkdir -p UpdateRepos
cd UpdateRepos
git clone https://gitflic.ru/project/prime-world-classic/content_pwc.git # production laucher
git clone https://gitflic.ru/project/prime-world-classic/content-test.git # test launcher
git clone https://gitflic.ru/project/prime-world-classic/pwc-gitupdates.git # production client
git clone https://gitflic.ru/project/prime-world-classic/pwc-gitupdates-test.git # test client

cd content_pwc
# gilab and github mirrors (production laucher)
git remote set-url --add origin https://gitlab.com/prime-world-classic/content.git
git remote set-url --add origin https://github.com/Prime-World-Classic/content.git
cd ..

cd content-test
# gilab and github mirrors (test laucher)
git remote set-url --add origin https://gitlab.com/prime-world-classic/content-test.git
git remote set-url --add origin https://github.com/Prime-World-Classic/pw-git-updates-test.git
cd ..

cd pwc-gitupdates
# gilab and github mirrors (production client)
git remote set-url --add origin https://gitlab.com/prime-world-classic/PWCGitUpdates.git
git remote set-url --add origin https://github.com/Prime-World-Classic/pw-git-updates.git
cd ..

cd pwc-gitupdates-test
# gilab and github mirrors (test client)
git remote set-url --add origin https://gitlab.com/prime-world-classic/pwc-git-updates-test.git
git remote set-url --add origin https://github.com/Prime-World-Classic/pw-git-updates-test.git