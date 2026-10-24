#!/bin/sh
# Сборка gamedataNN.pile из Data/ — подробности в PilePack/README.md.
# Запуск из корня репозитория: ./pilepack.sh plan | build | verify | status | extract | clean
cd "$(dirname "$0")" || exit 1
PY=python3
command -v "$PY" >/dev/null 2>&1 || PY=python
exec "$PY" PilePack/pilepack.py "$@"
