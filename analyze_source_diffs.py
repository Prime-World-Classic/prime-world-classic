#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Анализ диффа ветки: показывает только файлы с реальными логическими изменениями.

Usage:
    ./analyze_source_diffs.sh [A B] [options]      (обёртка над этим скриптом)
    python3 analyze_source_diffs.py [A B] [options]

Позиционные аргументы:
    A — базовый коммит/ветка (по умолчанию: merge-base main HEAD)
    B — целевой коммит/ветка (по умолчанию: HEAD)

Опции:
    --files-only        выводит только имена файлов с логическими изменениями
                        (по одному на строку, без диффов и служебных строк)
    --ext EXT[,EXT...]  фильтрует вывод по расширению файла,
                        например: --ext cpp,h  или  --ext .cpp,inl

Не показываются (считаются неизменёнными):
  - удалённые файлы
  - бинарные файлы
  - файлы/строки, где diff — только кодировка (BOM, CRLF, NUL)
  - файлы/строки, где diff — только кириллица/не-ASCII символы
  - файлы/строки, где diff — только комментарии (в т.ч. кириллица в комментариях)
  - файлы/строки, где diff — только регистр символов (case-fix)

Фильтрация идёт и на уровне файла, и на уровне строк: внутри диффа
отбрасываются пары «-/+» строк, отличающиеся только по этим признакам.
Вывод намеренно содержит только логические изменения. О количестве
отфильтрованных строк/файлов скрипт не сообщает.
"""
import argparse
import difflib
import os
import re
import subprocess
import sys

NON_ASCII = re.compile(r"[^\x00-\x7F]")
WS = re.compile(r"\s+")


def run(args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)


def git_show(ref, path):
    r = run(["git", "show", "%s:%s" % (ref, path)])
    if r.returncode != 0:
        return None
    return r.stdout


def merge_base(b):
    r = run(["git", "merge-base", "main", b])
    if r.returncode == 0:
        return r.stdout.decode().strip()
    cur = run(["git", "rev-parse", "--abbrev-ref", "HEAD"]).stdout.decode().strip()
    r = run(["git", "merge-base", cur, b])
    if r.returncode != 0:
        sys.exit("не удалось определить merge-base")
    return r.stdout.decode().strip()


def prep(raw):
    """NUL, BOM, CRLF → нормализованный текст"""
    raw = raw.replace(b"\x00", b"")
    if raw.startswith(b"\xef\xbb\xbf"):
        raw = raw[3:]
    raw = raw.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1251", errors="replace")


def strip_comments(src):
    """Убирает // и /* */ комментарии с учётом строк и символьных литералов"""
    out = []
    i, n = 0, len(src)
    NORMAL, BLOCK, STR = 0, 1, 2
    state = NORMAL
    while i < n:
        c = src[i]
        if state == NORMAL:
            if c == "/" and i + 1 < n and src[i + 1] == "/":
                j = src.find("\n", i)
                if j == -1:
                    j = n
                out.append(" ")
                i = j
            elif c == "/" and i + 1 < n and src[i + 1] == "*":
                out.append(" ")
                i += 2
                state = BLOCK
            elif c == '"' or c == "'":
                out.append(c)
                i += 1
                while i < n and src[i] != c:
                    if src[i] == "\\" and i + 1 < n:
                        out.append(src[i:i + 2])
                        i += 2
                        continue
                    out.append(src[i])
                    i += 1
                if i < n:
                    out.append(src[i])
                    i += 1
            else:
                out.append(c)
                i += 1
        elif state == BLOCK:
            if c == "*" and i + 1 < n and src[i + 1] == "/":
                out.append(" ")
                i += 2
                state = NORMAL
            else:
                if c != "\n":
                    out.append(" ")
                i += 1
    return "".join(out)


def normalize(text, no_comments):
    if no_comments:
        text = strip_comments(text)
    text = NON_ASCII.sub(" ", text)
    text = WS.sub(" ", text)
    return text.strip()


def ext_matches(path, exts):
    if not exts:
        return True
    e = os.path.splitext(path)[1].lstrip(".").lower()
    return e in exts


def line_key(line):
    """Ключ строки для сравнения: без комментариев, кириллицы, с сжатыми пробелами"""
    t = strip_comments(line)
    t = NON_ASCII.sub(" ", t)
    t = WS.sub(" ", t)
    return t.strip()


def pair_noise(dels, adds):
    """Сопоставляет пары «-строк» и «+строк», отличающиеся только шумом
    (кириллица/пробелы/комментарии/регистр). Возвращает списки строк,
    которые нужно сохранить (с реальными изменениями)."""
    if not dels or not adds:
        return list(dels), list(adds)
    so = [line_key(x) for x in dels]
    sn = [line_key(x) for x in adds]
    used_o, used_n = set(), set()
    sm = difflib.SequenceMatcher(None, so, sn, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                used_o.add(i1 + k)
                used_n.add(j1 + k)
        elif tag == "replace":
            for a in range(i1, i2):
                if a in used_o:
                    continue
                for b in range(j1, j2):
                    if b in used_n:
                        continue
                    if so[a].lower() == sn[b].lower():
                        used_o.add(a)
                        used_n.add(b)
                        break
    keep_o = [x for i, x in enumerate(dels) if i not in used_o]
    keep_n = [x for j, x in enumerate(adds) if j not in used_n]
    return keep_o, keep_n


def filtered_diff(a, b, f):
    """git diff файла с построчной фильтрацией шума.
    Возвращает (header, hunks) или None, если логических изменений нет.
    header — строки заголовка (diff --git / index / --- / +++),
    hunks — список (hunk_header, [del...], [add...])."""
    r = run(["git", "diff", "--no-color", "-U0", "--ignore-cr-at-eol",
             "--ignore-space-at-eol", "--inter-hunk-context=0", a, b, "--", f])
    out = r.stdout.decode(errors="replace")
    header = []
    hunks = []
    lines = out.split("\n")
    i, n = 0, len(lines)
    while i < n:
        ln = lines[i]
        if ln.startswith("diff --git"):
            j = i
            while j < n and not lines[j].startswith("@@"):
                header.append(lines[j])
                j += 1
            i = j
            continue
        if ln.startswith("@@"):
            dels, adds = [], []
            i += 1
            while i < n and not lines[i].startswith("@@") \
                    and not lines[i].startswith("diff --git"):
                c = lines[i][:1]
                if c == "-":
                    dels.append(lines[i][1:])
                elif c == "+":
                    adds.append(lines[i][1:])
                i += 1
            keep_o, keep_n = pair_noise(dels, adds)
            if keep_o or keep_n:
                m = re.match(r"^@@ -(\d+)(?:,\d+)? \+(\d+)(?:,\d+)? @@", ln)
                old_start = int(m.group(1)) if m else 0
                new_start = int(m.group(2)) if m else 0
                hunks.append(("@@ -%d,%d +%d,%d @@" % (old_start, len(keep_o),
                                                       new_start, len(keep_n)),
                              keep_o, keep_n))
            continue
        i += 1
    if not hunks:
        return None
    return header, hunks


def main():
    ap = argparse.ArgumentParser(
        description="Показать файлы с реальными логическими изменениями (A → B)")
    ap.add_argument("a", nargs="?", default=None,
                    help="базовый коммит/ветка (по умолчанию: merge-base main HEAD)")
    ap.add_argument("b", nargs="?", default="HEAD",
                    help="целевой коммит/ветка (по умолчанию: HEAD)")
    ap.add_argument("--files-only", action="store_true",
                    help="только имена файлов, без диффов и служебных строк")
    ap.add_argument("--ext", default=None,
                    help="фильтр по расширению, через запятую: cpp,h,py (с точкой или без)")
    args = ap.parse_args()

    b = args.b
    a = args.a if args.a is not None else merge_base(b)

    exts = None
    if args.ext:
        exts = set()
        for e in args.ext.split(","):
            e = e.strip().lstrip(".").lower()
            if e:
                exts.add(e)

    r = run(["git", "diff", "--name-only", "--diff-filter=MAR", a, b])
    files = [f for f in r.stdout.decode().split("\n")
             if f and ext_matches(f, exts)]

    shown = []

    if not args.files_only:
        print("=== дифф: %s → %s ===" % (a, b))
        print()

    for f in files:
        r = run(["git", "diff", "--numstat", "--no-renames", a, b, "--", f])
        first = r.stdout.decode().split("\n")[0].split("\t")[0] if r.stdout else ""
        if first == "-":
            continue

        raw_a = git_show(a, f)
        raw_b = git_show(b, f)
        if raw_a is None or raw_b is None:
            # добавленный/переименованный файл — показываем целиком
            shown.append(f)
            if not args.files_only:
                subprocess.run(
                    ["git", "diff", "--no-color", "--ignore-cr-at-eol",
                     "--ignore-space-at-eol", a, b, "--", f],
                    stdout=sys.stdout)
                print()
            continue

        ta = prep(raw_a)
        tb = prep(raw_b)

        # Быстрый путь: файл целиком отличается только шумом — дифф не строим.
        if ta == tb:  # только кодировка (BOM/CRLF/NUL)
            continue
        na2 = normalize(ta, no_comments=True)
        nb2 = normalize(tb, no_comments=True)
        if na2 == nb2 or na2.lower() == nb2.lower():  # кириллица/комментарии/регистр
            continue

        # Реальные изменения: строим дифф с построчной фильтрацией шума
        fd = filtered_diff(a, b, f)
        if fd is None:
            continue
        header, hunks = fd
        shown.append(f)
        if not args.files_only:
            for h in header:
                print(h)
            for hdr, ko, kn in hunks:
                print(hdr)
                for x in ko:
                    print("-" + x)
                for x in kn:
                    print("+" + x)
            print()

    if args.files_only:
        for f in shown:
            print(f)
    else:
        print("Файлов с изменениями: %d" % len(shown))


if __name__ == "__main__":
    main()
