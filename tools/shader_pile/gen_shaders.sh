#!/usr/bin/env bash
# Генерация .shd на tiny10 (только там есть настоящий HLSL-компилятор:
# d3dcompiler_47.dll в System32; в Wine d3dcompiler_* — заглушки).
#
#   ./gen_shaders.sh            # фоновый прогон всего MaterialSpec.xml
#   ./gen_shaders.sh status     # прогресс (сколько материалов готово, индекс BigMaterial)
#   ./gen_shaders.sh <Имя>...   # прогон указанных материалов (быстро)
set -euo pipefail

GUEST="${GUEST:-pw-tiny10}"
SRC='C:\tools\hlsl'
XML="$SRC\\Src\\Render\\Shaders\\MaterialSpec.xml"
OUT="$SRC\\data\\Shaders\\UberShaders\\"
LOG="$SRC\\gen_all.log"

ssh_run() { timeout ${2:-300} ssh -o ConnectTimeout=5 -o ServerAliveInterval=15 "$GUEST" "$1" 2>&1 \
  | grep -v "post-quantum\|store now\|decrypt later\|may need to be upgraded"; }

case "${1:-run}" in
  status)
    timeout 120 scp -q -o ConnectTimeout=5 "$GUEST:C:/tools/hlsl/gen_all.log" /tmp/gen_all.log 2>/dev/null || true
    printf "processing=%s done=%s shd=%s\n" \
      "$(grep -c 'shader processing' /tmp/gen_all.log || true)" \
      "$(grep -c 'shader done' /tmp/gen_all.log || true)" \
      "$(ssh_run "dir /b $OUT\\*.shd 2>nul | find /c \".shd\"" | tr -d '[:space:]')"
    echo -n "последний индекс: "; tr -s ' \r' '\n' < /tmp/gen_all.log | grep -E '^[0-9]+$' | tail -1
    echo "ошибки:"; grep -oE "E[0-9]{4}|Can't open [^ ]*" /tmp/gen_all.log | sort | uniq -c | head -5
    ;;
  run)
    nohup timeout 5400 ssh -o ConnectTimeout=5 -o ServerAliveInterval=15 "$GUEST" \
      "$SRC\\sc\\ShaderCompiler.exe $XML --out$OUT > $LOG 2>&1" >/tmp/gen_all_ssh.log 2>&1 &
    echo "запущено фоном (pid $!), лог гостя: $LOG; прогресс: $0 status"
    ;;
  *)
    for name in "$@"; do
      echo "== $name"
      ssh_run "$SRC\\sc\\ShaderCompiler.exe $XML --out$OUT --name$name > $SRC\\gen_$name.log 2>&1 & type $SRC\\gen_$name.log | findstr /C:\"done\" /C:\"Total\" /C:\"E5017\"" 900
    done
    ;;
esac
