@echo off
setlocal

if not exist UpdateRepos mkdir UpdateRepos
cd UpdateRepos

git clone https://gitflic.ru/project/prime-world-classic/content_pwc.git
git clone https://gitflic.ru/project/prime-world-classic/content-test.git
git clone https://gitflic.ru/project/prime-world-classic/pwc-gitupdates.git
git clone https://gitflic.ru/project/prime-world-classic/pwc-gitupdates-test.git

cd content_pwc
git remote set-url --add origin https://gitlab.com/prime-world-classic/content.git
git remote set-url --add origin https://github.com/Prime-World-Classic/content.git
cd ..

cd content-test
git remote set-url --add origin https://gitlab.com/prime-world-classic/content-test.git
git remote set-url --add origin https://github.com/Prime-World-Classic/pw-git-updates-test.git
cd ..

cd pwc-gitupdates
git remote set-url --add origin https://gitlab.com/prime-world-classic/PWCGitUpdates.git
git remote set-url --add origin https://github.com/Prime-World-Classic/pw-git-updates.git
cd ..

cd pwc-gitupdates-test
git remote set-url --add origin https://gitlab.com/prime-world-classic/pwc-git-updates-test.git
git remote set-url --add origin https://github.com/Prime-World-Classic/pw-git-updates-test.git

endlocal