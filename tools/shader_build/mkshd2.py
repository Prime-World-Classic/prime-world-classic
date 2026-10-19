#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mkshd2.py — писатель контейнера `.shd` v2 (шаг 11 очереди этапа 3).

Зачем. Поставляемый контейнер содержит один слой — SM3 DXBC, который понимает
только D3D9. Vulkan-бэкенду (шаг 12) нужен SPIR-V, и он обязан лежать в ТОМ ЖЕ
контейнере под тем же индексом пары: индекс = комбинация пинов, а пины клиент
знает только в рантайме. Два файла рядом с материалом = два места рассинхрона.

Формат (обратная совместимость через « appended section», как в PE/DXBC):

  [v1 часть — байт в байт как сейчас]      u32 shadersCount
                                           u32 offsets[2*shadersCount] (абсолютные)
                                           блобы (общие по CRC)
  [секция цели, одна на формат]            u32 tag; u32 entryCount
                                           entryCount x {u32 index; u32 vsOff; u32 vsSize;
                                                          u32 psOff; u32 psSize}
                                           блобы этой цели

Размеры в записи цели (20 Б, а не 12) — не «на всякий случай»: D3D9-читателю
длина не нужна (`CreateVertexShader` сама идёт по блобу до конца), а SPIR-V в
Vulkan передаётся парой «указатель + длина» (`IDevice::createShader(desc,
binary, binarySize)`), и выводить длину из соседних смещений нельзя — блобы
дедуплицированы и идут в произвольном порядке относительно записей.
  [заголовок v2]                           'V2H\\0'; u32 version; u32 v1Size; u32 targetCount
                                           targetCount x {u32 tag; u32 off; u32 count}
  [трэйлер 16 Б в конце файла]             'SHD2'; u32 v2HeaderOffset; u32 version; 'SHD2'

Старый читатель (`Src/Render/multishader.cpp`) читает заголовок, таблицу и блобы
по абсолютным смещениям и никогда не выходит за конец v1-части — значит старый
клиент такой файл принимает и видит ровно то же, что раньше. Новый читатель
смотрит в последние 16 байт.

tag: 0 = SM3 DXBC (лежит в v1-части, отдельной секции не требует),
     1 = DXBC SM5, 2 = SPIR-V.

SM3-слой НЕ перегенерируется: он копируется из существующего контейнера.
Паритет генератора под Wine доказан (34/35, §6 «шаг 11.0»), а перегенерация =
смена картинки = визуальный гейт без необходимости. Прежняя претензия
«`TerrainElementMaterial.shd` устарел относительно исходников» НЕ
подтвердилась: перегенерация `ShaderCompiler.exe` из текущих исходников
(2026-10-16, `--clean --nameTerrainElementMaterial` и `--nameNatureMapMaterial`)
даёт файлы, побайтово совпадающие с репозиторными.

Компиляция второй цели идёт тем же корпусом, что и замер бюджета: DXC +
прослойка SM3->SM6 (`tools/shader_build/dxcdrv.py`). Пины декодируются из
индекса так же, как в генераторе (`Src/ShaderCompiler/ShaderCompiler.cpp:186-201`).
SPIR-V собирается С `-Zpr` и С `PW_MODERN_ARRAYS=1` (SSBO-форма динамических
массивов, шаг 10) — иначе Vulkan-путь нерабочий по построению.

Компилируются только ЖИВЫЕ индексы (снимок `live_indices-*.json`, §6 «шаг
11.0»): пространство `BasicMaterial` — 36 864 индексов, из них живых 15.
Пропущенная комбинация = offset 0, в рантайме её закрывает фолбэк-компиляция.

Использование:
  mkshd2.py --live tools/shader_build/live_indices-2026-10-15-moba1p.json \
            --out /tmp/shd2 [--materials AOEMaterial,BasicMaterial] [--verify]
"""

import argparse
import hashlib
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import dxcdrv                                           # noqa: E402
import spirv_iface                                      # noqa: E402

TAG_SM3, TAG_DXBC_SM5, TAG_SPIRV = 0, 1, 2
TAG_NAME = {TAG_SM3: "sm3", TAG_DXBC_SM5: "dxbc_sm5", TAG_SPIRV: "spirv"}
NAME_TAG = {v: k for k, v in TAG_NAME.items()}
SPIRV_MAGIC = 0x07230203

# Макрос шва исходников (шаг 10): динамические массивы уходят из c-регистров в
# StructuredBuffer. Для SM3-ветки он инертен (сверка 32/32 DXBC), для SPIR-V
# обязателен: неконстантная индексация члена UBO в Vulkan = неопределённое
# поведение, в D3D12 запрещена прямо.
MODERN_DEFINE = "PW_MODERN_ARRAYS=1"


# ------------------------------------------------------------------ чтение v1
def read_v1(data):
    """(shadersCount, таблица смещений) по байтам v1-части.

    Таблица v1: 2*shadersCount uint32, абсолютные смещения; блоб i=2*p (VS),
    i=2*p+1 (PS); одинаковые блобы разделяются (смещения совпадают).
    """
    if len(data) < 4:
        raise SystemExit("слишком короткий .shd (%d байт)" % len(data))
    count = struct.unpack_from("<I", data, 0)[0]
    table_off = 4
    need = table_off + 4 * 2 * count
    if len(data) < need:
        raise SystemExit("таблица на %d пар не влезает (%d байт)"
                         % (count, len(data)))
    table = list(struct.unpack_from("<%dI" % (2 * count), data, table_off))
    return data, count, table


def strip_trailer(data):
    """Если вход уже v2 — отрезать секции, заголовок и трэйлер, оставить v1-часть.

    Резать по v2HeaderOffset НЕЛЬЗЯ: секции лежат ПЕРЕД заголовком, поэтому
    старый слой попадал в «v1-часть» мёртвым грузом (замер: BasicMaterial
    2 133 940 -> 2 317 708 Б, а v1Size = 2 133 896 = старый v2HeaderOffset,
    т.е. писатель принял чужие секции за v1). Режем по v1Size из заголовка.
    """
    if len(data) >= 16:
        tail = data[-16:]
        if tail[:4] == b"SHD2" and tail[12:16] == b"SHD2":
            v2off, _ver = struct.unpack_from("<II", tail, 4)
            if v2off + 16 <= len(data):
                tag, _ver2, v1size, _tcnt = struct.unpack_from("<4sIII", data, v2off)
                if tag == b"V2H\0" and v1size <= v2off:
                    return data[:v1size]
            return data[:v2off]
    return data


# ------------------------------------------------------------------- запись
class TargetSection:
    """Одна цель (формат) внутри v2-секции.

    Таблица предшествует блобам, поэтому смещения нельзя назначать по ходу
    компиляции: сначала собираются байты (с дедупликацией по хэшу), потом
    layout считается целиком. Иначе блобы легли бы поверх ещё не дописанной
    таблицы.
    """

    def __init__(self, tag):
        self.tag = tag
        self.entries = []          # (index, vs_key|None, ps_key|None)
        self.blobs = {}            # ключ -> байты, порядок вставки = порядок записи
        self.missing = 0

    def add(self, index, vs_bytes, ps_bytes):
        keys = []
        for blob in (vs_bytes, ps_bytes):
            if blob is None:
                self.missing += 1
                keys.append(None)
                continue
            key = hashlib.sha1(blob).digest()
            if key not in self.blobs:
                self.blobs[key] = blob
            keys.append(key)
        self.entries.append((index, keys[0], keys[1]))

    def size(self):
        return 8 + 20 * len(self.entries) + sum(len(b) for b in self.blobs.values())

    def emit(self, base):
        pos = {}
        off = base + 8 + 20 * len(self.entries)
        for key, blob in self.blobs.items():
            pos[key] = off
            off += len(blob)
        out = bytearray(struct.pack("<II", self.tag, len(self.entries)))
        for index, vs_key, ps_key in self.entries:
            sizes = (len(self.blobs[vs_key]) if vs_key else 0,
                     len(self.blobs[ps_key]) if ps_key else 0)
            out += struct.pack(
                "<IIIII", index,
                pos.get(vs_key, 0) if vs_key else 0, sizes[0],
                pos.get(ps_key, 0) if ps_key else 0, sizes[1])
        for key, blob in self.blobs.items():
            assert len(out) + base == pos[key], "смещение блоба уехало"
            out += blob
        return bytes(out)


def hsv_rgb(h, s=1.0, v=1.0):
    """HSV -> (r,g,b) 0..1 — палитра для пробы «цвет по паре»."""
    i = int(h * 6.0) % 6
    f = h * 6.0 - int(h * 6.0)
    p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    return [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]


def build(path, live_indices, pins, out_path, targets, tree, extra_defines,
          log):
    raw = strip_trailer(open(path, "rb").read())
    v1, count, table = read_v1(raw)
    total = dxcdrv.total_pairs(pins)
    if count != total:
        log("  !! %s: таблица v1 на %d пар, спека даёт %d — беру минимум"
            % (os.path.basename(path), count, total))
    src = os.path.join(dxcdrv.SH, os.path.basename(path)[:-4] + ".hlsl")

    sections = {t: TargetSection(t) for t in targets if t != TAG_SM3}
    compiled = []
    name = os.path.basename(path)[:-4]

    stats = {}
    bank_stats = {}
    for index in live_indices:
        decoded = dxcdrv.decode_index(index, pins)
        if decoded is None:
            log("  !! %s: индекс %d вне пространства пинов" % (path, index))
            compiled.append((index, None, None))
            continue
        defines = ["%s=%d" % (n, v) for n, v in decoded] + [MODERN_DEFINE] + \
            extra_defines
        blobs = {}
        for stage in ("vs", "ps"):
            entry = dxcdrv.ENTRY_VS if stage == "vs" else dxcdrv.ENTRY_PS
            if tree is not None:
                tree.build(stage, os.path.basename(path)[:-4], defines)
            code, err, spv = dxcdrv.compile_dxc(src, stage, entry, defines,
                                                tree=tree)
            if code != 0:
                first = (err.strip().splitlines() or ["?"])[0]
                log("  !! %s #%d %s: %s" % (os.path.basename(path), index,
                                            stage, first))
                blobs[stage] = None
                continue
            if tree is not None and getattr(tree, "bank_members", None):
                bank_stats.setdefault(index, {})[stage] = len(tree.bank_members)
            blobs[stage] = open(spv, "rb").read()
        compiled.append((index, blobs.get("vs"), blobs.get("ps")))
        stats[index] = [(len(blobs.get("vs") or b"")), (len(blobs.get("ps") or b""))]

    # --- канонические location varying-ей (шаг 12.1) -----------------------
    # DXC назначает location каждому шейдеру по порядку полей структуры, поэтому
    # у пары VS/PS один семантик лежит на разных location (в валидаторе —
    # VUID-RuntimeSpirv-OpEntryPoint-08743; у VS остаются дырки, которых нет в
    # вершинной декларации — VUID-...-Input-07904). D3D9 связывает пару по ИМЕНИ
    # семантики, Vulkan — по LOCATION, так что связку восстанавливаем здесь:
    # одна карта на контейнер, применяется ко всем SPIR-V-блобам до записи.
    # Клиент location не хардкодит (layout строится по SPIR-V), поэтому
    # перестановка безопасна и для вершинных атрибутов.
    if TAG_SPIRV in sections:
        sem = set()
        for _index, vs, ps in compiled:
            for b in (vs, ps):
                if b is None:
                    continue
                r, err = spirv_iface.interface_vars(b)
                if r is None:
                    log("  !! %s: SPIR-V не разобран: %s" % (name, err))
                    continue
                sem |= {v[1] for v in r[1] if not v[1].startswith("SV_")}
        locmap = spirv_iface.build_loc_map(sem)
        log("  канонические location (%d семантик): %s"
            % (len(locmap), ",".join("%s=%d" % kv for kv in
                                     sorted(locmap.items(),
                                           key=lambda kv: kv[1]))))
        cache = {}
        moved = 0
        for i, (index, vs, ps) in enumerate(compiled):
            new = []
            for b in (vs, ps):
                if b is None:
                    new.append(None)
                    continue
                if b not in cache:
                    aligned, ch = spirv_iface.remap_locations(b, locmap)
                    if aligned is None:
                        log("  !! %s #%d: %s — блок без выравнивания"
                            % (name, index, ch))
                        cache[b] = b
                    else:
                        bad = spirv_iface.check_collisions(
                            aligned, "%s #%d" % (name, index))
                        ok, err = spirv_iface.validate(aligned)
                        if bad or not ok:
                            log("  !! %s #%d после выравнивания: %s %s"
                                % (name, index, ";".join(bad), err))
                            cache[b] = b
                        else:
                            if ch:
                                moved += 1
                            cache[b] = aligned
                new.append(cache[b])
            compiled[i] = (index, new[0], new[1])
        log("  выровнено блоков: %d из %d" % (moved, len(cache)))

    # --- порядок матричных умножений (шаг 12.1) -----------------------------
    # DXC (-spirv -Zpr) трактует раскладку матриц в c-банке транспонированно
    # к DXBC (проверено на паре a.hlsl/b.hlsl обоими компиляторами: fxc даёт
    # dp4 o, v, c0..c3, DXC — OpMatrixTimesVector). Лечится перестановкой
    # операндов во всех матричных умножениях (spirv_iface.fix_matrix_mult).
    if TAG_SPIRV in sections:
        mcache = {}
        fixed = 0
        for i, (index, vs, ps) in enumerate(compiled):
            new = []
            for b in (vs, ps):
                if b is None:
                    new.append(None)
                    continue
                if b not in mcache:
                    patched, n = spirv_iface.fix_matrix_mult(b)
                    if n:
                        ok, err = spirv_iface.validate(patched)
                        if not ok:
                            log("  !! %s #%d: перестановка матриц не прошла "
                                "spirv-val: %s — блок без правки" % (name, index, err))
                            mcache[b] = b
                        else:
                            fixed += n
                            mcache[b] = patched
                    else:
                        mcache[b] = b
                new.append(mcache[b])
            compiled[i] = (index, new[0], new[1])
        if fixed:
            log("  переставлено матричных умножений (VTM<->MTV): %d" % fixed)

    # --- ОТЛАДКА (MKSHD2_FORCE_PS_COLOR=1): PS пишет константный цвет ------
    # Разделяет «геометрия не доходит до растра» (кадр всё равно пустой) и
    # «геометрия есть, но PS даёт чёрный» (кадр становится однотонным по форме
    # сцены). Режим =2: цвет свой у каждой пары (контейнер+индекс) — снимок
    # показывает, КАКОЙ МАТЕРИАЛ какие пиксели закрывает (при =1 все пары
    # красные и это неразличимо). Только локальный стенд, в контент не коммитить.
    # Фильтр проб: MKSHD2_PROBE_ONLY="FullScreenMaterial,UIFlash" — патчить PS
    # только перечисленных контейнеров (по подстроке имени). Без него проба
    # перекрывает собой весь кадр и вклад отдельных проходов неразличим.
    def probe_sel(tag):
        """Фильтр конкретной пробы: MKSHD2_PROBE_ONLY_<TAG>, иначе общий."""
        pon = [s.strip() for s in
               os.environ.get("MKSHD2_PROBE_ONLY_" + tag,
                              os.environ.get("MKSHD2_PROBE_ONLY", "")).split(",")
               if s.strip()]
        inc = [s for s in pon if not s.startswith("!")]
        exc = [s[1:] for s in pon if s.startswith("!")]
        hit = (not inc) or any(s in name for s in inc)
        return hit and not any(s in name for s in exc)
    # MKSHD2_PROBE_WHICH=first|last — какую выборку показывать (у
    # FullScreenMaterial первая = diffuseMap = сцена, последняя = warFog).
    pwhich = os.environ.get("MKSHD2_PROBE_WHICH", "first")

    fmode = os.environ.get("MKSHD2_FORCE_PS_COLOR", "0")
    if TAG_SPIRV in sections and probe_sel("COLOR") and fmode in ("1", "2"):
        fcache = {}
        forced = 0
        for i, (index, vs, ps) in enumerate(compiled):
            if ps is None:
                continue
            if ps not in fcache:
                rgba = (1.0, 0.0, 0.0, 1.0)
                if fmode == "2":
                    # цвет по паре: контейнер задаёт тон, индекс пары — оттенок
                    h = ((zlib.crc32(name.encode()) % 360) / 360.0 * 0.11
                         + (index * 0.1145)) % 1.0
                    rgba = tuple(hsv_rgb(h, 0.95, 0.95)) + (1.0,)
                    lg = os.environ.get("MKSHD2_LEGEND",
                                        os.path.join(os.environ.get("MKSHD2_OUT", "."),
                                                     "legend.txt"))
                    try:
                        with open(lg, "a") as fh:
                            fh.write("%s#%d %02x%02x%02x\n" % (
                                name, index,
                                int(rgba[0] * 255), int(rgba[1] * 255), int(rgba[2] * 255)))
                    except OSError:
                        pass
                patched, n = spirv_iface.force_ps_color(ps, rgba)
                if patched is None:
                    log("  !! %s #%d: force_ps_color: %s" % (name, index, n))
                    fcache[ps] = ps
                else:
                    ok, err = spirv_iface.validate(patched)
                    if not ok:
                        log("  !! %s #%d: force_ps_color не прошёл spirv-val: %s"
                            % (name, index, err[:120]))
                        fcache[ps] = ps
                    else:
                        forced += 1
                        fcache[ps] = patched
            compiled[i] = (index, vs, fcache[ps])
        if forced:
            log("  !! ОТЛАДКА: PS залит константным цветом в %d блоках" % forced)

    # --- ОТЛАДКА (MKSHD2_FORCE_PS_SAMPLE=1): PS пишет цвет ПЕРВОЙ выборки ---
    # Если кадр показывает текстуры — путь текстур рабочий, а чёрный даёт
    # освещение/константы; если кадр чёрный — чёрные сами текстуры.
    if TAG_SPIRV in sections and probe_sel("SAMPLE") and os.environ.get("MKSHD2_FORCE_PS_SAMPLE") == "1":
        scache = {}
        shown = 0
        for i, (index, vs, ps) in enumerate(compiled):
            if ps is None:
                continue
            if ps not in scache:
                patched, n = spirv_iface.force_ps_sample(ps, which=pwhich)
                if patched is None:
                    scache[ps] = ps
                else:
                    ok, err = spirv_iface.validate(patched)
                    scache[ps] = patched if ok else ps
                    if not ok:
                        log("  !! %s #%d: force_ps_sample не прошёл spirv-val: %s"
                            % (name, index, err[:100]))
                    else:
                        shown += 1
            compiled[i] = (index, vs, scache[ps])
        if shown:
            log("  !! ОТЛАДКА: PS выводит выборку из текстуры в %d блоках" % shown)

    # --- ОТЛАДКА (MKSHD2_FORCE_PS_VARYING=1): PS пишет первый varying ------
    # Чёрный кадр => сломан интерфейс VS->PS; цветной => varying-ы в порядке.
    if TAG_SPIRV in sections and probe_sel("VARYING") and os.environ.get("MKSHD2_FORCE_PS_VARYING") == "1":
        vcache = {}
        vshown = 0
        for i, (index, vs, ps) in enumerate(compiled):
            if ps is None:
                continue
            if ps not in vcache:
                patched, n = spirv_iface.force_ps_varying(
                    ps, which=int(os.environ.get("MKSHD2_PROBE_VARYING_IDX", "0")))
                if patched is None:
                    vcache[ps] = ps
                else:
                    ok, err = spirv_iface.validate(patched)
                    vcache[ps] = patched if ok else ps
                    if ok:
                        vshown += 1
                    else:
                        log("  !! %s #%d: force_ps_varying не прошёл spirv-val: %s"
                            % (name, index, err[:100]))
            compiled[i] = (index, vs, vcache[ps])
        if vshown:
            log("  !! ОТЛАДКА: PS выводит varying в %d блоках" % vshown)

    # --- ОТЛАДКА (MKSHD2_FORCE_PS_BANK=1): PS пишет член c-банка ----------
    # Последние три пробы (константа / varying / выборка текстуры) цветные, а
    # полный PS даёт чёрный -> непроверенной осталась только доставка констант:
    # если кадр с этой пробой цветной (для BasicMaterial первый float4-член банка
    # PS = ambientColor 0,6 -> серый), UBO константов доходит до шейдера.
    if TAG_SPIRV in sections and probe_sel("BANK") and os.environ.get("MKSHD2_FORCE_PS_BANK") == "1":
        bcache = {}
        bshown = 0
        for i, (index, vs, ps) in enumerate(compiled):
            if ps is None:
                continue
            if ps not in bcache:
                patched, info = spirv_iface.force_ps_bank(ps)
                if patched is None:
                    bcache[ps] = ps
                else:
                    ok, err = spirv_iface.validate(patched)
                    bcache[ps] = patched if ok else ps
                    if ok:
                        bshown += 1
                    else:
                        log("  !! %s #%d: force_ps_bank не прошёл spirv-val: %s"
                            % (name, index, err[:100]))
            compiled[i] = (index, vs, bcache[ps])
        if bshown:
            log("  !! ОТЛАДКА: PS выводит член c-банка в %d блоках" % bshown)

    # --- ОТЛАДКА (MKSHD2_FORCE_PS_SAMPLE_AT=<u>,<v>): PS пишет выборку из ----
    # текстуры в ФИКСИРОВАННОЙ точке UV. Снимает с проверки координаты: чёрный
    # и здесь -> чёрная сама текстура на GPU (загрузка/привязка), цветной ->
    # сломан путь UV. Фильтр материалов: MKSHD2_PROBE_ONLY_SAMPLE_AT.
    sat = os.environ.get("MKSHD2_FORCE_PS_SAMPLE_AT", "")
    if TAG_SPIRV in sections and sat and probe_sel("SAMPLE_AT"):
        uv = tuple(x.strip() for x in sat.split(","))[:2]
        if len(uv) < 2:
            uv = (uv[0] if uv else "0.5", "0.5")
        acache = {}
        ashown = 0
        for i, (index, vs, ps) in enumerate(compiled):
            if ps is None:
                continue
            if ps not in acache:
                patched, info = spirv_iface.force_ps_sample_at(ps, uv)
                if patched is None:
                    acache[ps] = ps
                else:
                    ok, err = spirv_iface.validate(patched)
                    acache[ps] = patched if ok else ps
                    if ok:
                        ashown += 1
                    else:
                        log("  !! %s #%d: force_ps_sample_at не прошёл spirv-val: %s"
                            % (name, index, err[:100]))
            compiled[i] = (index, vs, acache[ps])
        if ashown:
            log("  !! ОТЛАДКА: PS выбирает в UV=(%s,%s) в %d блоках"
                % (uv[0], uv[1], ashown))

    # --- ОТЛАДКА (MKSHD2_FORCE_PS_ID=<класс:N|%id>): PS пишет выбранное -----
    # значение из середины арифметики. Пробы выше показали: геометрия, UV,
    # нормали, текстуры и константы в порядке, а полный PS даёт чёрный. Эта
    # проба идёт по потоку данных PS снизу вверх (sample -> fadd -> fmul -> dot)
    # и указывает, на какой операции значение становится нулём.
    pid = os.environ.get("MKSHD2_FORCE_PS_ID", "")
    if TAG_SPIRV in sections and pid and probe_sel("ID"):
        icache = {}
        ishown = 0
        for i, (index, vs, ps) in enumerate(compiled):
            if ps is None:
                continue
            if ps not in icache:
                patched, info = spirv_iface.force_ps_id(ps, pid)
                if patched is None:
                    icache[ps] = ps
                else:
                    ok, err = spirv_iface.validate(patched)
                    icache[ps] = patched if ok else ps
                    if ok:
                        ishown += 1
                    else:
                        log("  !! %s #%d: force_ps_id не прошёл spirv-val: %s"
                            % (name, index, err[:100]))
            compiled[i] = (index, vs, icache[ps])
        if ishown:
            log("  !! ОТЛАДКА: PS выводит значение %s в %d блоках" % (pid, ishown))

    for index, vs, ps in compiled:
        for sec in sections.values():
            sec.add(index, vs, ps)

    if bank_stats:
        mx = max((max(v.values()) for v in bank_stats.values() if v), default=0)
        log("  канонический банк: пар %d, членов на пару макс %d"
            % (len(bank_stats), mx))
    out = bytearray(v1)
    base = len(out)
    offsets = []
    for tag in sorted(sections):
        sec = sections[tag]
        offsets.append((tag, base, len(sec.entries)))
        blob = sec.emit(base)
        out += blob
        base += len(blob)
    # v1Size обязателен: читатель v2 копирует в таблицу ТОЛЬКО v1-часть
    # (иначе к каждому контейнеру приплюсовываются байты SPIR-V-секций), и по
    # этому же размеру он проверяет границы смещений.
    hdr = struct.pack("<4sIII", b"V2H\0", 1, len(v1), len(offsets))
    for tag, off, cnt in offsets:
        hdr += struct.pack("<III", tag, off, cnt)
    v2hdr_off = base
    out += hdr
    out += struct.pack("<4sII4s", b"SHD2", v2hdr_off, 1, b"SHD2")
    open(out_path, "wb").write(bytes(out))
    return count, sections, stats, len(v1), len(out)


# ------------------------------------------------------------------- сверка
def verify(path, log, ref_path=None):
    data = open(path, "rb").read()
    if ref_path:
        ref = open(ref_path, "rb").read()
        if not data.startswith(ref):
            log("  !! v1-часть НЕ совпадает с эталоном %s — SM3-слой изменился"
                % ref_path)
            return False
    if len(data) < 16 or data[-16:][:4] != b"SHD2" or data[-16:][12:16] != b"SHD2":
        log("  v2 НЕТ (только v1)")
        return False
    v2hdr, ver = struct.unpack_from("<II", data, len(data) - 12)
    hdr = data[v2hdr:]
    magic, hver, v1size, tcount = struct.unpack_from("<4sIII", hdr, 0)
    assert magic == b"V2H\0" and hver == ver == 1, "заголовок v2 не бьётся"
    ok = True
    v1, count, _table = read_v1(data[:v2hdr])
    # v1Size — конец v1-части; секции целей идут после неё, поэтому он обязан
    # быть не больше смещения заголовка v2 (равен ему, только если целей нет).
    if v1size < 8 or v1size > v2hdr:
        log("  !! v1Size=%d вне диапазона [8, %d]" % (v1size, v2hdr))
        ok = False
    else:
        # смещения v1-таблицы обязаны оставаться внутри v1-части: иначе читатель
        # v1 (или наш, если цели ещё не поддерживаются) уйдёт в SPIR-V
        bad_v1 = 0
        for o in _table:
            # 0xFFFFFFFF — «пустая» запись (писатель v1 заполняет таблицу 0xFF
            # перед расчётом смещений), её проверять не на что
            if o and o != 0xFFFFFFFF and not (4 < o < v1size):
                bad_v1 += 1
        if bad_v1:
            log("  !! %d смещений v1-таблицы вне v1-части [4,%d)" % (bad_v1, v1size))
            ok = False
    spans = []
    for i in range(tcount):
        tag, off, cnt = struct.unpack_from("<III", hdr, 16 + 12 * i)
        # конец секции = начало следующей секции, а у последней — заголовок v2.
        nxt = struct.unpack_from("<III", hdr, 16 + 12 * (i + 1))[1] \
            if i + 1 < tcount else v2hdr
        spans.append((tag, off, cnt, nxt))
    for tag, off, cnt, sec_end in spans:
        sec = data[off:sec_end]
        stag, scnt = struct.unpack_from("<II", sec, 0)
        if stag != tag or scnt != cnt:
            log("  !! секция %s: заголовок не совпал" % TAG_NAME.get(tag, tag))
            ok = False
            continue
        # блобы идут ПОСЛЕ таблицы из 20-байтных записей; смещения абсолютные
        blob_lo = off + 8 + 20 * cnt
        checked = bad = 0
        smallest = None
        for j in range(cnt):
            index, vs_off, vs_size, ps_off, ps_size = \
                struct.unpack_from("<IIIII", sec, 8 + 20 * j)
            for o, sz in ((vs_off, vs_size), (ps_off, ps_size)):
                if o == 0:
                    if sz:
                        log("  !! запись #%d: смещение 0, но размер %d" % (index, sz))
                        ok = False
                    bad += 1
                    continue
                checked += 1
                if not sz:
                    log("  !! запись #%d: смещение %d с нулевым размером" % (index, o))
                    ok = False
                    continue
                if o < blob_lo or o + sz > sec_end:
                    log("  !! блоб %d+%d вне области цели %s [%d,%d)"
                        % (o, sz, TAG_NAME.get(tag, tag), blob_lo, sec_end))
                    ok = False
                    continue
                if tag == TAG_SPIRV:
                    m = struct.unpack_from("<I", data, o)[0]
                    if m != SPIRV_MAGIC:
                        log("  !! по смещению %d не SPIR-V (0x%08x)" % (o, m))
                        ok = False
                smallest = o if smallest is None else min(smallest, o)
        log("  цель %-8s индексов %d, блобов %d, пропусков %d, блок с %d"
            % (TAG_NAME.get(tag, tag), cnt, checked, bad,
               smallest if smallest is not None else 0))
    log("  v1-часть: %d байт (v1Size), пар %d; с секциями %d байт"
        % (v1size, count, len(data)))
    return ok


# --------------------------------------------------------------------- CLI
def main():
    ap = argparse.ArgumentParser(description="писатель .shd v2")
    ap.add_argument("--in", dest="indir", default=dxcdrv.UBER,
                    help="каталог с v1-контейнерами (по умолчанию UberShaders)")
    ap.add_argument("--out", required=True, help="каталог результата")
    ap.add_argument("--live", required=True,
                    help="JSON живых индексов (tools/shader_build/live_indices-*.json)")
    ap.add_argument("--materials", help="список через запятую (по умолчанию все)")
    ap.add_argument("--targets", default="spirv",
                    help="цели через запятую: sm3,dxbc_sm5,spirv")
    ap.add_argument("--extra-define", action="append", default=[],
                    metavar="NAME=ЗНАЧЕНИЕ")
    ap.add_argument("--verify", action="store_true",
                    help="только разобрать уже готовые файлы из --out")
    ap.add_argument("--ref", help="каталог эталонов v1 для --verify "
                                  "(побайтовая сверка SM3-слоя)")
    ap.add_argument("--keep-tree", action="store_true")
    ap.add_argument("--flat-sets", action="store_true",
                    help="НЕ раскладывать ресурсы по descriptor set (макет §3.z "
                         "п.1 по умолчанию: $Globals set0, SSBO set1, текстуры "
                         "set2, сэмплеры set3); нужно только для сравнения")
    ap.add_argument("--cbank", action="store_true",
                    help="канонический c-банк: вместо авто-$Globals — один "
                         "`cbuffer pwCBankVS/PS` с packoffset по номерам c из "
                         "исходников (set 0, binding 0/1); см. §3.z п.2")
    args = ap.parse_args()

    def log(*a):
        print(*a)
        sys.stdout.flush()

    os.makedirs(args.out, exist_ok=True)
    targets = [NAME_TAG[t.strip()] for t in args.targets.split(",") if t.strip()]
    live = json.load(open(args.live))
    if "materials" in live:                       # снимок collect_live_indices.py
        live = live["materials"]
    mats = dict(dxcdrv.spec_materials())

    if args.verify:
        bad = 0
        for fn in sorted(os.listdir(args.out)):
            if not fn.endswith(".shd"):
                continue
            log("== %s" % fn)
            ref = os.path.join(args.ref, fn) if args.ref else None
            if not verify(os.path.join(args.out, fn), log, ref):
                bad += 1
        log("ИТОГ: %s" % ("все контейнеры разобраны" if not bad
                          else "%d с ошибками" % bad))
        return 1 if bad else 0

    want = set(m.strip() for m in args.materials.split(",")) if args.materials \
        else None
    tree = None if TAG_SPIRV not in targets else dxcdrv.ShimTree(
        set_layout=not args.flat_sets, cbank=args.cbank)
    total_missing = 0
    done = 0
    for name in sorted(live):
        if want and name not in want:
            continue
        path = os.path.join(args.indir, name + ".shd")
        if not os.path.isfile(path):
            log("!! нет контейнера: %s" % path)
            continue
        if name not in mats:
            log("!! %s нет в MaterialSpec.xml — пины взять негде" % name)
            continue
        indices = sorted(live[name].get("indices", []))
        log("== %s: %d живых индексов" % (name, len(indices)))
        count, sections, stats, v1size, outsize = build(
            path, indices, mats[name], os.path.join(args.out, name + ".shd"),
            targets, tree, args.extra_define, log)
        for tag, sec in sections.items():
            total_missing += sec.missing
            log("   цель %s: %d индексов, %d блобов, %d байт v1 -> %d байт"
                % (TAG_NAME[tag], len(sec.entries), len(sec.blobs),
                   v1size, outsize))
        done += 1
    if tree and not args.keep_tree:
        tree.cleanup()
    log("ИТОГ: контейнеров %d, пропущенных комбинаций %d" % (done, total_missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
