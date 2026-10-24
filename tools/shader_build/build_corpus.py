#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""build_corpus.py — сборка корпуса `.shd` v2 (цель SPIR-V) без упаковки в pile.

Зачем. Писатель (`mkshd2.py`) и сборщик онлайн-сайдкаров (`online_shaders/
build_all.py`) — два шага, у каждого свои обязательные флаги; между ними нужен
гейт «SM3-слой не поехал». Скрипт прогоняет шаги и гейт и складывает артефакты
в ОДИН каталог. В pile он НЕ пакует (паковка — отдельный, осознанный шаг:
`tools/shader_pile/mkpile.py`, см. вывод скрипта в конце).

Куда попадают артефакты (важно: они разные по природе):
  <out>/<Материал>.shd            — готовые v2-контейнеры, плоско, без подкаталогов
  <out>/OnlineShaders/*.spv       — с --online (пересборка) или --online-copy
                                    (копия готовых); при --online build_all.py
                                    пишет ПОЗЫЧНО в Data/Shaders/OnlineShaders,
                                    т.е. правит отслеживаемые git-файлы
  <out>/build.log                 — полный лог компиляции (DXC печатает много)
  pw/branches/r1117/Src/Render/zzz_build_shaders_*/  — дерево-прослойка, живёт
                                    во время прогона и удаляется (в .gitignore);
                                    --keep-tree оставляет его для разбора
Ничего из этого не попадает в Data/Shaders/UberShaders/ — репозиторный контент
скрипт не трогает (кроме --online, см. предупреждение). Правильный порядок
дальше: артефакты КАЧАЮТСЯ В Data (`.shd` -> Data/Shaders/UberShaders/, `.spv` ->
Data/Shaders/OnlineShaders/), оттуда их забирает `./pilepack.sh build` (PilePack,
манифест `PilePack/gamedata_manifest.json`), а готовые `Piles/gamedataNNN.pile`
уезжают на тестовый стенд (репо `game-pwc`, ветка `test`). Промежуточный пайк
`tools/shader_pile/mkpile.py` — только для разовых экспериментов, он минует Data
и потому не воспроизводим.

Использование:
  tools/shader_build/build_corpus.py                     # весь корпус (union)
  tools/shader_build/build_corpus.py --materials SkyMaterial,SilhouetteMaterial
  tools/shader_build/build_corpus.py --online --out /tmp/shd_build_$(date +%F)
Прогон долгий — запускать фоном: nohup tools/shader_build/build_corpus.py \
  >/tmp/corpus.log 2>&1 &
"""

import argparse
import datetime
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
BR = os.path.abspath(os.path.join(TOOLS, "..", "pw", "branches", "r1117"))

MKSHD2 = os.path.join(HERE, "mkshd2.py")
BUILD_ALL = os.path.join(TOOLS, "online_shaders", "build_all.py")
UBER = os.path.join(BR, "Data", "Shaders", "UberShaders")
ONLINE_REPO = os.path.join(BR, "Data", "Shaders", "OnlineShaders")
DXC = os.path.join(TOOLS, "spirv_probe", "vendor", "dxc", "bin", "dxc")
DEFAULT_LIVE = os.path.join(HERE, "live_indices-2026-10-15-union.json")

TAG_NAME = {0: "SM3", 1: "DXBC SM5", 2: "SPIR-V"}


def sh(cmd, logfh, cwd=None):
    """Запуск с потоковым логом: строки идут и в консоль, и в build.log."""
    logfh.write("$ " + " ".join(cmd) + "\n")
    logfh.flush()
    p = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, bufsize=1,
                         universal_newlines=True)
    for line in p.stdout:                     # инкрементально, без накопления
        sys.stdout.write(line)
        sys.stdout.flush()
        logfh.write(line)
    logfh.flush()
    return p.wait()


def read_v2(path):
    """Заголовок v2 -> (v1Size, [(tag, off, count)]) или None, если v2 нет."""
    with open(path, "rb") as f:
        f.seek(-16, os.SEEK_END)
        tail = f.read(16)
        if tail[:4] != b"SHD2" or tail[12:16] != b"SHD2":
            return None
        hdr_off = struct.unpack("<I", tail[4:8])[0]
        f.seek(hdr_off)
        hdr = f.read(16)
        if hdr[:4] != b"V2H\0":
            return None
        _ver, v1size, tcount = struct.unpack("<III", hdr[4:16])
        targets = []
        for _ in range(tcount):
            tag, off, count = struct.unpack("<III", f.read(12))
            targets.append((tag, off, count))
        return v1size, targets


def entries_of(path, off):
    with open(path, "rb") as f:
        f.seek(off)
        tag, n = struct.unpack("<II", f.read(8))
    return tag, n


def parity_rows(outdir, refdir):
    """SM3-слой обязан остаться побайтово прежним: new[:v1Size] == ref[:v1Size].

    `mkshd2.py --verify --ref` для этого НЕ годится: он требует
    new.startswith(ref) всем файлом, а репозиторные контейнеры уже v2 со своими
    секциями -> ложные «N с ошибками».
    """
    bad = 0
    rows = []
    for fn in sorted(os.listdir(outdir)):
        if not fn.endswith(".shd"):
            continue
        p = os.path.join(outdir, fn)
        v2 = read_v2(p)
        if v2 is None:
            rows.append((fn, "v2 НЕТ", "-", "-", "-"))
            bad += 1
            continue
        v1size, targets = v2
        ref = os.path.join(refdir, fn)
        if not os.path.isfile(ref):
            rows.append((fn, "нет эталона", v1size, "-", os.path.getsize(p)))
            bad += 1
            continue
        rv2 = read_v2(ref)
        rsize = rv2[0] if rv2 else os.path.getsize(ref)
        with open(p, "rb") as f:
            new = f.read(v1size)
        with open(ref, "rb") as f:
            old = f.read(rsize)
        same = (rsize == v1size and new == old)
        if not same:
            bad += 1
        tg = ",".join("%s:%d" % (TAG_NAME.get(t, t),
                                 entries_of(p, off)[1]) for t, off, _ in targets)
        rows.append((fn, "OK" if same else "РАСХОЖДЕНИЕ", v1size, tg,
                     os.path.getsize(p)))
    return rows, bad


def copy_online_spv(dst, log):
    """Копия репозиторных .spv в каталог артефактов (pile потом берёт оба слоя)."""
    os.makedirs(dst, exist_ok=True)
    n = 0
    for fn in sorted(os.listdir(ONLINE_REPO)):
        if not fn.endswith(".spv"):
            continue
        with open(os.path.join(ONLINE_REPO, fn), "rb") as f:
            data = f.read()
        with open(os.path.join(dst, fn), "wb") as f:
            f.write(data)
        n += 1
    log("   .spv в %s: %d" % (dst, n))
    return n


def main():
    ap = argparse.ArgumentParser(description="сборка корпуса .shd v2 без pile")
    ap.add_argument("--out", default="/tmp/shd_build",
                    help="каталог артефактов (плоские <Материал>.shd)")
    ap.add_argument("--live", default=DEFAULT_LIVE,
                    help="снимок живых индексов (по умолчанию union профилей)")
    ap.add_argument("--in", dest="indir", default=UBER,
                    help="каталог v1-контейнеров-источников")
    ap.add_argument("--materials", help="подмножество через запятую")
    ap.add_argument("--targets", default="spirv", help="см. mkshd2.py")
    ap.add_argument("--no-cbank", action="store_true",
                    help="собрать без канонического c-банка (репозиторный корпус "
                         "собран С --cbank: в SPIR-V pwCBankVS/PS, $Globals нет)")
    ap.add_argument("--flat-sets", action="store_true", help="см. mkshd2.py")
    ap.add_argument("--keep-tree", action="store_true",
                    help="не удалять Src/Render/zzz_build_shaders_* (разбор)")
    ap.add_argument("--extra-define", action="append", default=[])
    ap.add_argument("--online", action="store_true",
                    help="пересобрать онлайн-сайдкары .spv (ВНИМАНИЕ: "
                         "build_all.py пишет ПОЗЫЧНО в Data/Shaders/OnlineShaders "
                         "= правит отслеживаемые git-файлы)")
    ap.add_argument("--online-copy", action="store_true",
                    help="только скопировать готовые репозиторные .spv в --out "
                         "(для будущей упаковки; дерево не трогает)")
    ap.add_argument("--no-verify", action="store_true")
    args = ap.parse_args()

    for what, p in (("DXC", DXC), ("снимок живых индексов", args.live),
                    ("писатель", MKSHD2), ("каталог v1", args.indir)):
        if not os.path.exists(p):
            print("!! нет %s: %s" % (what, p))
            return 2
    os.makedirs(args.out, exist_ok=True)
    logpath = os.path.join(args.out, "build.log")

    print("== артефакты: %s" % args.out)
    print("   %s" % ", ".join(sorted(os.path.basename(f) for f in
                                     [args.live, args.indir])))
    rc_all = 0
    with open(logpath, "w") as logfh:
        cmd = [sys.executable, MKSHD2, "--live", args.live,
               "--in", args.indir, "--out", args.out, "--targets", args.targets]
        if not args.no_cbank:
            cmd.append("--cbank")
        if args.flat_sets:
            cmd.append("--flat-sets")
        if args.keep_tree:
            cmd.append("--keep-tree")
        if args.materials:
            cmd += ["--materials", args.materials]
        for d in args.extra_define:
            cmd += ["--extra-define", d]
        print("\n== 1. mkshd2 (корпус UberShaders)")
        rc = sh(cmd, logfh)
        rc_all |= rc

        if args.online:
            print("\n== 2. онлайн-сайдкары (пишут в репозиторный каталог!)")
            rc = sh([sys.executable, BUILD_ALL], logfh)
            rc_all |= rc
            copy_online_spv(os.path.join(args.out, "OnlineShaders"), print)
            try:
                st = subprocess.run(["git", "-C", os.path.join(TOOLS, ".."),
                                     "status", "--porcelain", "--",
                                     "pw/branches/r1117/Data/Shaders/OnlineShaders"],
                                    stdout=subprocess.PIPE, universal_newlines=True)
                ch = [l for l in st.stdout.splitlines() if l.strip()]
                print("   изменённых отслеживаемых .spv в дереве: %d" % len(ch))
            except OSError:
                pass
        elif args.online_copy:
            print("\n== 2. копирование готовых .spv (без пересборки)")
            copy_online_spv(os.path.join(args.out, "OnlineShaders"), print)

        if not args.no_verify:
            print("\n== 3. разбор заголовков (--verify, без --ref)")
            rc = sh([sys.executable, MKSHD2, "--verify", "--out", args.out,
                     "--live", args.live, "--in", args.indir], logfh)
            if rc:
                rc_all |= rc

        print("")
        rows, bad = parity_rows(args.out, args.indir)
        head = ("\n== SM3-слой (new[:v1Size] vs ref[:v1Size])\n"
                "%-34s %-13s %10s  %-22s %10s"
                % ("контейнер", "v1-слой", "v1Size", "цели(пар)", "байт"))
        body = "\n".join("%-34s %-13s %10s  %-22s %10s" % r for r in rows)
        print(head)
        print(body)
        logfh.write(head + "\n" + body + "\n")
        logfh.flush()
        if bad:
            rc_all |= 1

    print("\n== итог: rc=%d, артефакты в %s (лог: %s)" % (rc_all, args.out, logpath))
    print("   в Data и в pile НЕ упаковано. Канонический путь (фикс живёт в Data):")
    print("     cp %s/*.shd pw/branches/r1117/Data/Shaders/UberShaders/" % args.out)
    print("     cp %s/OnlineShaders/*.spv pw/branches/r1117/Data/Shaders/OnlineShaders/"
          % args.out)
    print("     ./pilepack.sh build && ./pilepack.sh verify   # -> Piles/gamedataNNN.pile")
    print("   на тестовый стенд — только изменившиеся pile (привязка файла к pile")
    print("   зафиксирована в манифесте), синхронизацией с game-pwc ветки test.")
    return rc_all


if __name__ == "__main__":
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    print("# build_corpus.py %s" % stamp)
    sys.exit(main())
