#!/bin/sh
# sem_probe.sh — проба семантики индекса экземпляра (шаг 9/11 очереди,
# docs/PLAN_NVRHI.md §2.10 «шаг 2», §3.z п.4-п.5).
#
# Отвечает на три вопроса без запуска на стенде:
#   1) какой ТИП обязателен у SV_InstanceID (ответ: uint — см. README.md);
#   2) раскрывается ли макрос в позиции семантики и во всём объявлении
#      (т.е. можно ли держать ОДИН исходник и выбирать форму в драйвере
#      компиляции — шаг 11);
#   3) во что DXC раскладывает индекс экземпляра в SPIR-V (BuiltIn InstanceIndex).
#
# Запуск: tools/sem_probe/sem_probe.sh [фильтр-подстрока цели]
# DXC берётся из tools/spirv_probe/vendor/dxc/bin/dxc (вендоринг, этап 1).

set -u
cd "$(dirname "$0")/../.." || exit 1
DXC=tools/spirv_probe/vendor/dxc/bin/dxc
SRC=tools/sem_probe/sem_probe.hlsl
OUT=${TMPDIR:-/tmp}/sem_probe
mkdir -p "$OUT"
FILTER=${1:-}

if [ ! -x "$DXC" ]; then
  echo "нет DXC: $DXC" >&2
  exit 1
fi

# Цель: DXBC (путь DX12; DXC при этом поднимает профиль до SM6 — см. README)
# и SPIR-V (путь Vulkan). Профиль vs_5_0 оставлен намеренно: он показывает,
# что DXC его не компилирует «как есть».
run() {
  _mode=$1; shift
  _name=$1; shift
  case "$_name" in
    *"$FILTER"*) ;;
    *) return ;;
  esac
  _out=$OUT/sem_mode${_mode}_${_name}
  _log=$("$@" -Fo "$_out" "$SRC" 2>&1)
  _rc=$?
  _err=$(printf '%s\n' "$_log" | grep -m1 -iE '^error|error:' | cut -c1-90)
  printf 'mode=%-2s %-16s rc=%s %s\n' "$_mode" "$_name" "$_rc" "$_err"
}

for n in 0 1 2 3 4 5 6 7 8 9; do
  run "$n" dxbc  "$DXC" -T vs_5_0 -E ProbeVS -D PROBE_MODE=$n
  run "$n" spirv "$DXC" -spirv -T vs_6_0 -E ProbeVS -D PROBE_MODE=$n
done

# Режим 9 «как в драйвере»: объявление приходит извне (-D), а не из файла.
run 9d dxbc  "$DXC" -T vs_5_0 -E ProbeVS -D PROBE_MODE=9 \
  -D "INSTANCE_INDEX_DECL=uint inst : SV_InstanceID"
run 9d spirv "$DXC" -spirv -T vs_6_0 -E ProbeVS -D PROBE_MODE=9 \
  -D "INSTANCE_INDEX_DECL=uint inst : SV_InstanceID"

# Что DXC кладёт в SPIR-V для uint-варианта (OpDecorate ... BuiltIn InstanceIndex).
sed 's/int    inst : SV_InstanceID/uint   inst : SV_InstanceID/' "$SRC" > "$OUT/sem_uint.hlsl"
"$DXC" -spirv -T vs_6_0 -E ProbeVS -D PROBE_MODE=1 -Fo "$OUT/sem_uint.spv" "$OUT/sem_uint.hlsl" 2>/dev/null
python3 - "$OUT/sem_uint.spv" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
w = struct.unpack('<%dI' % (len(d) // 4), d)
BUILTIN = {42: 'VertexIndex', 43: 'InstanceIndex'}
out = []
i = 5
while i < len(w):
    wc, op = w[i] >> 16, w[i] & 0xFFFF
    if op == 71 and wc >= 4 and w[i + 2] == 11:      # OpDecorate, BuiltIn
        out.append(BUILTIN.get(w[i + 3], w[i + 3]))
    i += wc
print('SPIR-V BuiltIn-входы:', out or 'нет')
PY
