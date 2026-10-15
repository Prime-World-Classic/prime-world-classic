#!/usr/bin/env bash
# Проба: клиент под Wine с DXVK (d3d9=n,b) вместо wined3d.
# Замер: доходит ли до боя, что печатает ConfigPins, рисуется ли 3D-сцена.
# DXVK — только тестовая обвязка (PLAN_NVRHI §2.15 п.13), в продукт не идёт.
set -u
DISPLAY_NUM=:97
PREFIX="$HOME/.wine-pw"
BIN=/home/rekon/PW/wine-client/Game/Bin
LOGDIR_ROOT="$HOME/Документы/My Games/Prime World Classic/Session/Logs"
TAG=${1:-dxvk}
RUNLOG=/tmp/${TAG}_run.log
SHOT=/tmp/${TAG}_shot.png

# 1. сессия (заново на каждый прогон)
URL=$(cd /home/rekon/PW && timeout 90 docker compose -f pw-api-docker/docker-compose.dev.yml \
  exec -T -e PW_VERSION=2.16.1 api node cli/session.js --ips 7f000001 2>&1 \
  | grep -oE "pwclassic://runGame/[a-f0-9]+/2\.16\.1/2/7f000001/27300")
if [ -z "$URL" ]; then echo "NEED_URL"; exit 2; fi
echo "URL=$URL"

# 2. запуск
cd "$BIN"
HUDOPT=(); [ -n "${HUD:-}" ] && HUDOPT=(DXVK_HUD="$HUD")
DISPLAY=$DISPLAY_NUM WINEPREFIX=$PREFIX WINEDLLOVERRIDES="${OVERRIDE:-d3d9=n,b}" \
  DXVK_LOG_LEVEL=info "${HUDOPT[@]}" \
  nohup wine ./PW_Game.exe protocol "$URL" -dumpConfigPins -dumpUnitStats \
  > "$RUNLOG" 2>&1 &
echo "started pid=$!"
sleep "${WAIT:-120}"

# 3. лог клиента
L=$(ls -t "$LOGDIR_ROOT" | head -1)
OUT="$LOGDIR_ROOT/$L"
echo "LOGDIR=$L"
grep -hoE "ConfigPins: .*" "$OUT"/*output.txt | head -3
echo "--- markers:"
for m in "Entered lobby" "Got lobby instance" "Connected to game service" \
         "mapload: all map objects loaded" "ShaderContainer:" "ShaderTarget:" \
         "нужен рантайм-компилятор" "DepthFormat:" "Direct3DCreate9"; do
  printf '%s = %s\n' "$m" "$(grep -hc "$m" "$OUT"/*output.txt 2>/dev/null | paste -sd+ | bc 2>/dev/null)"
done
echo "--- unitstats: $(ls "$OUT" | grep -c unitstats)"
echo "--- auto_screenshot files:"; ls -la "$BIN"/pw_auto_* 2>/dev/null
echo "--- tail:"; tail -4 "$OUT"/*output.txt
# 4. stderr wine / DXVK
echo "=== dxvk/wine stderr (head 40):"; head -40 "$RUNLOG"
echo "=== dxvk errors:"; grep -icE "err:|DXVK-\[E\]" "$RUNLOG"
grep -iE "DXVK-\[E\]" "$RUNLOG" | head -10
# 5. снимок
DISPLAY=$DISPLAY_NUM import -window root "$SHOT" && echo "shot=$SHOT"
# 6. убить клиент (иначе следующий прогон молча выйдет: один инстанс)
pkill -x PW_Game.exe; sleep 4; pgrep -x PW_Game.exe && echo "STILL_RUNNING"
