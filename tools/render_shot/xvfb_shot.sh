#!/usr/bin/env bash
# Снимок окна клиента прямо с Xvfb (DISPLAY по умолчанию :97).
#
# Зачем: у обоих бэкендов штатный readback кадра работает по-разному. DXVK на
# этом стенде сегодня отдаёт чёрные кадры через ReadSurfaceRenderTarget (замер
# 2026-10-20: pw_auto_bb_t60..t140.png = 786432/786432 чёрных), а Vulkan-бэкенд
# не отдаёт кадр через ReadSurface вообще (plan: auto_screenshot вешает
# waitForIdle). Снимок с X-сервера не зависит ни от бэкенда, ни от клиентского
# readback — это единственный путь получить A/B одного и того же стенда.
#
# Использование:
#   xvfb_shot.sh ВЫХОД.png [--name "Prime World Classic"] [--display :97]
#   xvfb_shot.sh --series ВЫХОД-префикс СЕК_МЕЖДУ СКОЛЬКО_КАДРОВ
#     (пишет ВЫХОД-префикс_t<сек>.png; время — от старта скрипта)
#
# Окно ищется по имени (Wine подписывает окно «Prime World Classic - pwclassic -
# <версия>»), берётся client area целиком — без кропа и без предположений о
# позиции окна. Требует ImageMagick (import) и xwininfo.
set -u
D=${DISPLAY:-:97}
NAME="Prime World Classic"

find_win() {
  DISPLAY=$D xwininfo -root -tree 2>/dev/null \
    | grep -oE "0x[0-9a-f]+ \"$NAME[^\"]*\"" | grep -oE "0x[0-9a-f]+" | head -1
}

case "${1:-}" in
  --series)
    out=$2; step=${3:-30}; n=${4:-4}
    mkdir -p "$(dirname "$out")" 2>/dev/null
    for i in $(seq 0 $((n-1))); do
      w=$(find_win)
      [ -z "$w" ] && { echo "окно не найдено (пропуск t$((i*step)))" >&2; sleep $step; continue; }
      DISPLAY=$D import -window "$w" "${out}_t$((i*step)).png" 2>/dev/null \
        && echo "${out}_t$((i*step)).png"
      [ $i -lt $((n-1)) ] && sleep $step
    done
    ;;
  *)
    [ -z "${1:-}" ] && { sed -n '2,20p' "$0"; exit 2; }
    while [ $# -gt 1 ]; do
      case $1 in
        --name) NAME=$2; shift 2 ;;
        --display) D=$2; shift 2 ;;
        *) break ;;
      esac
    done
    out=$1
    mkdir -p "$(dirname "$out")" 2>/dev/null
    w=$(find_win)
    [ -z "$w" ] && { echo "окно «$NAME» не найдено на $D" >&2; exit 1; }
    DISPLAY=$D import -window "$w" "$out" && echo "$out ($w)"
    ;;
esac
