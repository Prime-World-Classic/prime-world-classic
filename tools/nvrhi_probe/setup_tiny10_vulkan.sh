#!/usr/bin/env bash
# setup_tiny10_vulkan.sh — ставит в tiny10 software-Vulkan (Mesa lavapipe) и
# гоняет там же Windows-пробу.
#
#   ./setup_tiny10_vulkan.sh            # раскладка + запуск + вывод пробы
#   ./setup_tiny10_vulkan.sh --install  # только раскладка
#
# Зачем. В tiny10 (Win10 LTSC 17763, урезанная) вырезано всё Vulkan-обвязочное:
#   - C:\Windows\System32\vulkan-1.dll отсутствует (в обычной 17763 он есть);
#   - видео — QEMU std VGA -> «Microsoft Basic Display Adapter», драйверного
#     Vulkan нет и быть не может.
# Собираем стек сами: vulkan-1.dll (наш, build_loader_wine.sh, /MT) +
# ICD Mesa lavapipe (vendor/mesa-win/vulkan_lvp.dll, software-растер).
#
# Два обязательных условия, без которых не заведётся (проверено):
#   1. ICD регистрируется В РЕЕСТРЕ (HKLM\SOFTWARE\Khronos\Vulkan\Drivers).
#      VK_ICD_FILENAMES НЕ РАБОТАЕТ: проба под SSH запускается с правами
#      администратора, а загрузчик в этом случае игнорирует переменные
#      окружения («Loader is running with elevated permissions») и не находит
#      ни одного ICD -> vkCreateInstance = VK_ERROR_INCOMPATIBLE_DRIVER.
#   2. Пробу запускать В ИНТЕРАКТИВНОЙ СЕССИИ (задача без /ru, как PwE2E).
#      Из сессии 0 (чистый SSH) swapchain создаётся, но vkQueuePresentKHR
#      падает в VK_ERROR_MEMORY_MAP_FAILED: lavapipe делает present через GDI
#      DIB, а в неинтерактивной сессии его нет.
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
GUEST="${PW_TINY10:-pw-tiny10}"
DEST='C:\tools\vulkan'
TASK=PwProbe
MESA="$HERE/vendor/mesa-win"
LOADER="$HERE/build-loader/loader/loader/vulkan-1.dll"
EXE="$HERE/build/nvrhi_probe.exe"

for f in "$MESA/vulkan_lvp.dll" "$MESA/lvp_icd.x86_64.json" "$LOADER" "$EXE"; do
  [ -f "$f" ] || { echo "ERROR: нет $f (fetch_vendor.sh / build_loader_wine.sh / build_probe_wine.sh)" >&2; exit 1; }
done

clean() { grep -viE "post-quantum|store now|may need to be upgraded"; }

echo "== раскладка на $GUEST:$DEST"
ssh -o ConnectTimeout=8 "$GUEST" "if not exist $DEST mkdir $DEST" 2>&1 | clean >/dev/null
scp -q "$MESA/vulkan_lvp.dll" "$MESA/lvp_icd.x86_64.json" "$LOADER" "$EXE" "$GUEST:C:/tools/vulkan/" \
  || { echo "ERROR: scp не удался" >&2; exit 1; }

echo "== регистрация ICD в реестре"
ssh -o ConnectTimeout=8 "$GUEST" \
  'reg add "HKLM\SOFTWARE\Khronos\Vulkan\Drivers" /v "C:\tools\vulkan\lvp_icd.x86_64.json" /t REG_DWORD /d 0 /f' 2>&1 | clean

cat > "$HERE/_run_vulkan.cmd" <<'CMDEOF'
@echo off
cd /d C:\tools\vulkan
nvrhi_probe.exe > C:\tools\vulkan\probe.log 2>&1
echo == done >> C:\tools\vulkan\probe.log
CMDEOF
scp -q "$HERE/_run_vulkan.cmd" "$GUEST:C:/tools/vulkan/run_vulkan.cmd" >/dev/null

if [ "${1:-}" = "--install" ]; then
  echo "== готово (только раскладка)"
  exit 0
fi

# Задача БЕЗ /ru -> выполняется в интерактивной сессии залогоненного
# пользователя (иначе present не работает, см. шапку).
echo "== запуск в интерактивной сессии (задача $TASK)"
ssh -o ConnectTimeout=8 "$GUEST" \
  "schtasks /delete /tn $TASK /f & schtasks /create /tn $TASK /tr \"C:\tools\vulkan\run_vulkan.cmd\" /sc once /st 00:00 /f & del C:\tools\vulkan\probe.log 2>nul & schtasks /run /tn $TASK" 2>&1 | clean

for i in $(seq 1 30); do
  sleep 3
  if ssh -o ConnectTimeout=8 "$GUEST" "findstr /c:\"== done\" C:\tools\vulkan\probe.log" 2>/dev/null | clean | grep -q done; then
    break
  fi
done

echo "== вывод пробы:"
ssh -o ConnectTimeout=8 "$GUEST" "type C:\tools\vulkan\probe.log" 2>&1 | clean
