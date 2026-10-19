#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pilepack.py — кросс-платформенная замена FilePileCompiler.exe (Windows-пайплайн
PW_PackData/PackData.ps1 + PW_PackData/FilePileCompiler/process_data_pile.py).

Читает дерево Data, отбирает файлы по policy.txt, раскладывает их по фиксированным
спискам (gamedata_manifest.json) и пишет контейнеры gamedataNN.pile в Piles/.

Формат контейнера повторяет FilePileCompiler.cpp байт-в-байт
(pw/branches/r1117/Src/System/FileSystem/FilePileFormat.h, FilePileCompiler.cpp,
System/SimpleCrypt.hpp), поэтому результат читает клиентский FilePileLoader без
изменений в коде игры. Требования и описание решения — README.md в этой папке.

Подкоманды:
  build    (по умолчанию) собрать/дообновить pile
  plan     то же, что build --dry-run: показать план, ничего не писать
  verify   проверить собранные pile (структура, crc, лимиты загрузчика, дубли ключей)
  status   сводка по манифесту
  clean    удалить pile-файлы из выходной папки

Только стандартная библиотека, Python 3.8+.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import mmap
import multiprocessing
import os
import re
import struct
import sys
import time
import zlib

# --------------------------------------------------------------------------------------
# Формат pile (FilePileFormat.h)
# --------------------------------------------------------------------------------------

FILEPILE_TAG = 0x656C6950          # 'Pile'
FILEPILE_VERSION = 0x00010002      # загрузчик отвергает другие версии
FILEPILE_COMPRESSED = 0x0001

TAIL = struct.Struct('<6I')        # tag, version, filesNumber, stringsOffset, stringsLength, fileIndexOffset
ENTRY = struct.Struct('<6I')       # size, sourceSize, offset, flags, fileNameOffset, crc
CRYPT_BASE = 83                    # SimpleCrypt::baseNum

# Лимиты загрузчика (FilePileLoader.cpp / FilePileCompiler.cpp). Их нарушение на клиенте
# выглядит как «pile is corrupt» (модалка, убивающая процесс) или как молчаливый пропуск
# контейнера: PileFileSystem::ScanForPiles не сообщает об ошибке Init().
LOADER_MAX_FILES_PER_PILE = 1024 * 1024
LOADER_MAX_STRINGS = 16 * 1024 * 1024
LOADER_MAX_FILE_SIZE = 256 * 1024 * 1024      # dev-вар fs_piled_file_size_limit = 256 МБ
COMPILER_MAX_PILE_SIZE = 0x7FFFFFFF           # WriteBytes() оригинала обрывает запись здесь

MB = 1024 * 1024
MMAP_THRESHOLD = 4 * MB                       # файлы крупнее читаются через mmap

DEFAULT_TARGET = 50 * MB      # целевой размер собранного pile
DEFAULT_SPLIT = 60 * MB       # выше этого размера список файлов pile делится пополам
DEFAULT_LEVEL = 6             # zlib level (оригинал — 9; см. README «Скорость»)
DEFAULT_RATIO = 0.90          # max_comp_ratio: сжатие применяется, если ratio <= значения
DEFAULT_MIN_CS = 1            # comp_size: файлы больше этого размера пробуют сжимать
DEFAULT_EST_RATIO = 0.50      # оценка упакованного размера при первой раскладке
DEFAULT_PREFIX = 'gamedata'

# Оценка упакованного размера файла (для первой раскладки по 50 МБ). Замерена на Data
# (zlib level 6, выборка по расширениям): для мелких файлов сжатие почти не работает,
# плюс на каждый файл приходится ~70 байт индекса и имени.
EST_RATIO_BY_EXT = {
    'ani': 0.15, 'anim': 0.63, 'baked_terrain': 0.35, 'codes': 0.06, 'codes2': 0.17,
    'coll': 0.52, 'dds': 0.50, 'fev': 0.15, 'fsb': 0.88, 'heightmap': 0.48, 'hlsl': 0.47,
    'layer_mask': 0.05, 'lua': 0.25, 'md5': 0.30, 'naturemap': 0.47, 'objectheight': 0.15,
    'part': 0.58, 'psb': 0.58, 'road': 0.52, 'shd': 0.09, 'shd2': 0.03, 'shgrid': 0.12,
    'skel': 0.56, 'skin': 0.25, 'stat': 0.39, 'swf': 0.90, 'tcv_data': 0.02, 'ttf': 0.48,
    'txt': 0.15, 'vsb': 0.69, 'vtxcolor': 0.28, 'watermap': 0.02, 'xdb': 0.12,
}
EST_SMALL = 0.90              # файлы меньше EST_SMALL_SIZE почти не сжимаются
EST_SMALL_SIZE = 4096
EST_OVERHEAD = 70             # индекс (24 Б) + имя в таблице (~45 Б)

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DEFAULT_DATA = os.path.join(REPO, 'pw', 'branches', 'r1117', 'Data')
DEFAULT_OUT = os.path.join(REPO, 'Piles')
DEFAULT_POLICY = os.path.join(HERE, 'policy.txt')
DEFAULT_MANIFEST = os.path.join(HERE, 'gamedata_manifest.json')

MANIFEST_FORMAT = 2


def log(msg):
    sys.stderr.write(msg.rstrip('\n') + '\n')
    sys.stderr.flush()


def human(n):
    return '%.1f MB' % (n / float(MB))


# --------------------------------------------------------------------------------------
# SimpleCrypt (System/SimpleCrypt.hpp): state=0; на каждый байт state=(state+83)&0xFF, b^=state.
# Один непрерывный поток на всю таблицу имён (между именами НЕ сбрасывается);
# Reset() — только перед индексом. Хвост (tail) не шифруется.
# --------------------------------------------------------------------------------------

def crypt(buf, state=0):
    out = bytearray(len(buf))
    for i, b in enumerate(buf):
        state = (state + CRYPT_BASE) & 0xFF
        out[i] = b ^ state
    return bytes(out), state


# --------------------------------------------------------------------------------------
# Policy: что идёт в pile
# --------------------------------------------------------------------------------------

class Policy(object):
    """Формат policy.txt — директивы `ключ: значение` (см. README)."""

    def __init__(self, path):
        self.path = path
        self.ignore_dirs = set()
        self.exts = set()
        self.excludes = []        # [(снять_исключение, маска)] — в порядке файла, последнее совпадение важнее
        self.raw = ''
        self.load()

    def load(self):
        if not os.path.isfile(self.path):
            raise SystemExit('policy не найден: %s' % self.path)
        with open(self.path, 'rb') as f:
            self.raw = f.read().decode('utf-8', 'replace')
        for ln, line in enumerate(self.raw.splitlines(), 1):
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            key, _, val = line.partition(':')
            key = key.strip().lower()
            val = val.strip()
            if not val:
                raise SystemExit('%s:%d: пустое значение' % (self.path, ln))
            if key == 'ignore_dir':
                self.ignore_dirs.add(val.lower())
            elif key == 'ext':
                for e in val.split():
                    self.exts.add(e.lower().lstrip('*').lstrip('.'))
            elif key == 'exclude':
                v = val.lower()
                if v.startswith('!'):
                    self.excludes.append((True, v[1:]))
                else:
                    self.excludes.append((False, v))
            else:
                raise SystemExit('%s:%d: неизвестная директива %r' % (self.path, ln, key))
        if not self.exts:
            raise SystemExit('%s: пустой список ext' % self.path)

    def fingerprint(self):
        import hashlib
        return hashlib.sha256(self.raw.encode('utf-8')).hexdigest()[:12]

    def walk(self, data_dir):
        """Относительные (от Data) пути файлов, попадающих в pile; '/' как разделитель."""
        keys = []
        for dirpath, dirnames, filenames in os.walk(data_dir):
            dirnames[:] = [d for d in dirnames if d.lower() not in self.ignore_dirs]
            for name in filenames:
                low = name.lower()
                if '.' not in low:
                    continue
                if low.rsplit('.', 1)[1] not in self.exts:
                    continue
                rel = os.path.relpath(os.path.join(dirpath, name), data_dir).replace(os.sep, '/')
                rel_low = rel.lower()
                excluded = False
                for negate, m in self.excludes:
                    # маска без '/' сравнивается с именем файла, с '/' — с полным относительным путём
                    if fnmatch.fnmatch(rel_low if '/' in m else low, m):
                        excluded = not negate
                if excluded:
                    continue
                keys.append(rel)
        return keys


# --------------------------------------------------------------------------------------
# Скан файлов: {rel: (size, crc32)}
# --------------------------------------------------------------------------------------

def file_crc(path, chunk=4 * 1024 * 1024):
    crc = 0
    with open(path, 'rb') as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            crc = zlib.crc32(b, crc)
    return crc & 0xFFFFFFFF


def _stat_chunk(args):
    """
    crc считаем у всех файлов каждого прогона: на всю Data это 4,6 с (crc32 ~1,9 ГБ/с),
    а время правки файлу доверять нельзя — правка «тот же размер, другие байты» по
    размеру и mtime не видна, а её обязан ловить именно скан.
    """
    data_dir, chunk = args
    res = []
    for rel in chunk:
        p = os.path.join(data_dir, rel.replace('/', os.sep))
        try:
            st = os.stat(p)
        except OSError as e:
            res.append((rel, -1, 0, 'stat: %s' % e))
            continue
        try:
            crc = file_crc(p)
        except OSError as e:
            res.append((rel, st.st_size, 0, 'read: %s' % e))
            continue
        res.append((rel, st.st_size, crc, None))
    return res


def _map(fn, arglist, jobs):
    if jobs <= 1 or len(arglist) <= 1:
        return [fn(a) for a in arglist]
    pool = multiprocessing.Pool(jobs)
    try:
        return pool.map(fn, arglist)
    finally:
        pool.close()
        pool.join()


def scan(data_dir, keys, jobs):
    """{rel: (size, crc32)}. Крупные файлы первыми: они задают длину хвоста параллельной части."""
    sized = []
    for k in keys:
        try:
            sized.append((os.path.getsize(os.path.join(data_dir, k.replace('/', os.sep))), k))
        except OSError:
            sized.append((0, k))
    sized.sort(key=lambda t: (-t[0], t[1]))

    out = {}
    chunks, chunk_bytes = [], 0
    for size, k in sized:
        chunks.append(k)
        chunk_bytes += size
        if chunk_bytes >= 16 * MB:
            _merge_scan(out, _map(_stat_chunk, [(data_dir, chunks)], jobs))
            chunks, chunk_bytes = [], 0
    if chunks:
        _merge_scan(out, _map(_stat_chunk, [(data_dir, chunks)], jobs))
    return out


def _merge_scan(out, results):
    for rows in results:
        for rel, size, crc, err in rows:
            if err:
                raise SystemExit('скан %s: %s' % (rel, err))
            out[rel] = (size, crc)


# --------------------------------------------------------------------------------------
# Манифест (привязка файлов к pile)
# --------------------------------------------------------------------------------------

def est_size(rel, size, fallback=DEFAULT_EST_RATIO):
    """Оценка вклада файла в размер pile (для раскладки до первой сборки)."""
    ext = rel.rsplit('.', 1)[1].lower() if '.' in rel else ''
    r = EST_SMALL if size < EST_SMALL_SIZE else EST_RATIO_BY_EXT.get(ext, fallback)
    return EST_OVERHEAD + int(size * r)


class Pile(object):
    __slots__ = ('id', 'name', 'files', 'packed_size')

    def __init__(self, pid, name=None, files=None, packed_size=0):
        self.id = pid
        self.name = name
        self.files = files if files is not None else []   # [rel, size, crc]
        self.packed_size = packed_size

    def source_size(self):
        return sum(f[1] for f in self.files)


class Manifest(object):
    def __init__(self, path):
        self.path = path
        self.params = {}
        self.piles = []
        self.next_id = 1
        self.exists = False

    def load(self):
        if not os.path.isfile(self.path):
            return
        self.exists = True
        with open(self.path, 'rb') as f:
            data = json.loads(f.read().decode('utf-8'))
        if data.get('format') not in (1, MANIFEST_FORMAT):
            raise SystemExit('%s: неподдерживаемый format=%r' % (self.path, data.get('format')))
        self.params = data.get('params', {})
        self.piles = []
        for p in data.get('piles', []):
            # v1 хранил ещё mtime_ns 4-м полем — он больше не нужен, crc считается у всех
            files = [[e[0], e[1], e[2]] for e in p.get('files', [])]
            self.piles.append(Pile(p['id'], p.get('name'), files, p.get('packed_size', 0)))
        self.next_id = max([p.id for p in self.piles] + [0]) + 1

    def save(self, params):
        """JSON с одним файлом на строку: diff в git остаётся читаемым."""
        self.params = params
        L = ['{', '  "format": %d,' % MANIFEST_FORMAT,
             '  "params": %s,' % json.dumps(self.params, sort_keys=True),
             '  "piles": [']
        for i, p in enumerate(self.piles):
            L.append('    {')
            L.append('      "id": %d,' % p.id)
            L.append('      "name": %s,' % json.dumps(p.name))
            L.append('      "packed_size": %d,' % p.packed_size)
            L.append('      "source_size": %d,' % p.source_size())
            L.append('      "files": [')
            for j, f in enumerate(p.files):
                L.append('        %s%s' % (json.dumps(f), ',' if j + 1 < len(p.files) else ''))
            L.append('      ]')
            L.append('    }%s' % (',' if i + 1 < len(self.piles) else ''))
        L.append('  ]')
        L.append('}')
        tmp = self.path + '.tmp'
        with open(tmp, 'wb') as f:
            f.write(('\n'.join(L) + '\n').encode('utf-8'))
        os.replace(tmp, self.path)


# --------------------------------------------------------------------------------------
# Сборка одного pile (вызывается в воркере; пишет атомарно через .tmp)
# --------------------------------------------------------------------------------------

def build_pile(task):
    out_path, data_dir, rels, level, min_cs, ratio = task
    entries = []      # [stored_size, source_size, offset, flags, name_off, crc, rel]
    records = []      # [rel, source_size, crc]
    off = 4
    n_comp = n_raw = 0
    content_bytes = 0
    tmp = out_path + '.tmp'

    with open(tmp, 'wb') as f:
        f.write(struct.pack('<I', FILEPILE_TAG))
        for rel in rels:
            p = os.path.join(data_dir, rel.replace('/', os.sep))
            mm = None
            try:
                if os.path.getsize(p) > MMAP_THRESHOLD:
                    # крупные файлы (голосовые банки до десятков МБ) не кладём в кучу дважды
                    fd = os.open(p, os.O_RDONLY)
                    mm = mmap.mmap(fd, 0, access=mmap.ACCESS_READ)
                    os.close(fd)
                    data = mm
                else:
                    with open(p, 'rb') as src:
                        data = src.read()
            except OSError as e:
                if mm is not None:
                    mm.close()
                return {'error': 'не удалось прочитать %s: %s' % (rel, e), 'name': out_path}
            src_size = len(data)
            crc = zlib.crc32(data) & 0xFFFFFFFF
            stored, flags = data, 0
            if min_cs >= 0 and src_size > min_cs:
                c = zlib.compress(data, level)
                if src_size and float(len(c)) / src_size <= ratio:
                    stored, flags = c, FILEPILE_COMPRESSED
            if flags:
                n_comp += 1
            else:
                n_raw += 1
            content_bytes += len(stored)
            f.write(stored)
            entries.append([len(stored), src_size, off, flags, 0, crc, rel])
            records.append([rel, src_size, crc])
            off += len(stored)
            if mm is not None:
                mm.close()
            else:
                del data
            del stored

        # таблица имён: один непрерывный поток шифра на все имена, имя с завершающим NUL
        strings_off = off
        buf = bytearray()
        state = 0
        for e in entries:
            e[4] = len(buf)
            enc, state = crypt(os.fsencode(e[6]) + b'\x00', state)
            buf += enc
        f.write(bytes(buf))
        strings_len = len(buf)

        # индекс: поток шифра с обнулённым state (SimpleCrypt::Reset перед индексом)
        index_off = off + strings_len
        blob = bytearray()
        state = 0
        for e in entries:
            enc, state = crypt(ENTRY.pack(e[0], e[1], e[2], e[3], e[4], e[5]), state)
            blob += enc
        f.write(bytes(blob))

        f.write(TAIL.pack(FILEPILE_TAG, FILEPILE_VERSION, len(entries),
                          strings_off, strings_len, index_off))
        total = index_off + len(blob) + TAIL.size
        os.fsync(f.fileno())
    os.replace(tmp, out_path)
    return {'name': out_path, 'packed_size': total, 'records': records,
            'compressed': n_comp, 'raw': n_raw, 'content': content_bytes}


def rebuild(piles, ids, data_dir, out_dir, args):
    """Пересобирает pile с id в `ids`; возвращает число собранных контейнеров."""
    renumber(piles, args.prefix)
    tasks = [(os.path.join(out_dir, p.name), data_dir, [f[0] for f in p.files],
              args.level, args.min_cs, args.ratio) for p in piles if p.id in ids]
    if not tasks:
        return 0
    t = time.time()
    if args.jobs > 1 and len(tasks) > 1:
        results = _map(build_pile, tasks, args.jobs)
    else:
        results = [build_pile(tsk) for tsk in tasks]
    for r in results:
        if r.get('error'):
            raise SystemExit(r['error'])
    by_name = {os.path.join(out_dir, p.name): p for p in piles}
    for r in results:
        p = by_name[r['name']]
        p.packed_size = r['packed_size']
        p.files = r['records']
    log('сборка: %d pile за %.1f с' % (len(results), time.time() - t))
    return len(results)


# --------------------------------------------------------------------------------------
# Раскладка: имена, деление переполненных списков
# --------------------------------------------------------------------------------------

def name_for(idx, prefix):
    width = max(2, len(str(max(1, idx))))
    return '%s%0*d.pile' % (prefix, width, idx)


def renumber(piles, prefix):
    for i, p in enumerate(piles, 1):
        p.name = name_for(i, prefix)


def split_oversized(piles, man, split_bytes):
    """
    Список файлов pile, перешагнувшего split_bytes, делится пополам по исходным байтам
    (сохраняя порядок -> части остаются непрерывными диапазонами). Рекурсивно, пока все
    части не станут ниже порога или не останутся однофайловыми. Возвращает (piles, ids).
    """
    need = set()
    changed = True
    while changed:
        changed = False
        out = []
        for p in piles:
            if p.packed_size > split_bytes and len(p.files) > 1:
                src = p.source_size()
                half = src / 2.0
                acc, cut = 0, 1
                for i, f in enumerate(p.files):
                    acc += f[1]
                    if acc >= half:
                        cut = i + 1
                        break
                cut = max(1, min(cut, len(p.files) - 1))
                a_src = sum(f[1] for f in p.files[:cut])
                a_pack = int(p.packed_size * a_src / src) if src else p.packed_size // 2
                a = Pile(p.id, None, p.files[:cut], a_pack)
                b = Pile(man.next_id, None, p.files[cut:], p.packed_size - a_pack)
                man.next_id += 1
                need.update([a.id, b.id])
                out.extend([a, b])
                changed = True
            else:
                out.append(p)
        piles = out
    return piles, need


def sync_names(piles, disk_names, out_dir, prefix):
    """
    Пересчитывает имена gamedataNN.pile и перекладывает уже готовые файлы на новые имена.
    Вызывается ДО пересборки: после деления pile сдвигаются вправо, и если сразу писать
    новую часть в освободившееся имя, оно затрёт содержимое соседнего pile. Сначала —
    перенос (через временное имя, чтобы цепочка a->b не перетирала b), потом сборка.
    disk_names: {id: имя файла на диске}, обновляется на месте.
    """
    renumber(piles, prefix)
    staged = []
    for p in piles:
        old = disk_names.get(p.id)
        if old and old != p.name:
            src = os.path.join(out_dir, old)
            if os.path.isfile(src):
                staged.append((src, os.path.join(out_dir, p.name), p.id))
    for src, dst, pid in staged:
        os.replace(src, os.path.join(out_dir, '.renam%d.pile' % pid))
    for src, dst, pid in staged:
        mid = os.path.join(out_dir, '.renam%d.pile' % pid)
        os.replace(mid, dst)
        log('  переименован: %s -> %s' % (os.path.basename(src), os.path.basename(dst)))
    disk_names.clear()
    refresh_disk_names(piles, disk_names, out_dir)


def refresh_disk_names(piles, disk_names, out_dir):
    """{id: имя файла на диске} по фактическому содержимому выходной папки."""
    disk_names.clear()
    for p in piles:
        if os.path.isfile(os.path.join(out_dir, p.name)):
            disk_names[p.id] = p.name


def disk_matches(pile, path):
    """
    Сверяет индекс на диске с манифестом (набор ключей и исходные размеры). Дешевле,
    чем пересборка, и ловит рассогласование после деления/ручных правок: молча оставить
    файл, который манифест считает другим pile, нельзя.
    """
    try:
        ents = pile_entries(path)
    except (SystemExit, Exception):
        return False
    if len(ents) != len(pile.files):
        return False
    return (sorted((e[0].lower(), e[2]) for e in ents) ==
            sorted((f[0].lower(), f[1]) for f in pile.files))


# --------------------------------------------------------------------------------------
# build
# --------------------------------------------------------------------------------------

def cmd_build(args):
    t0 = time.time()
    policy = Policy(args.policy)
    data_dir = os.path.abspath(args.data)
    out_dir = os.path.abspath(args.out)
    if not os.path.isdir(data_dir):
        raise SystemExit('Data не найдена: %s' % data_dir)
    os.makedirs(out_dir, exist_ok=True)

    keys = policy.walk(data_dir)
    log('policy: %d расширений, %d игнорируемых каталогов; отобрано файлов: %d'
        % (len(policy.exts), len(policy.ignore_dirs), len(keys)))

    # индекс внутри pile — case-insensitive hash_map: разные имена, отличающиеся регистром,
    # сливаются в один ключ (проигрывает последний). Не должно быть и в разных pile.
    seen = {}
    for k in keys:
        lk = k.lower()
        if lk in seen and seen[lk] != k:
            raise SystemExit('конфликт имён без учёта регистра: %s и %s' % (seen[lk], k))
        seen[lk] = k
    for k in keys:
        if k[:1] in ('0', '/'):
            raise SystemExit('имя %r начинается с %r — FilePileLoader отвергнет pile' % (k, k[0]))

    man = Manifest(args.manifest)
    man.load()
    man_piles = list(man.piles)

    params = {
        'target_bytes': args.target, 'split_bytes': args.split, 'level': args.level,
        'max_ratio': args.ratio, 'min_compress_size': args.min_cs, 'prefix': args.prefix,
        'policy_fingerprint': policy.fingerprint(),
    }
    full = args.force or not man.exists
    if man.exists and man.params and man.params != params:
        log('параметры сборки/policy изменились (%s -> %s): пересобираем всё'
            % (man.params.get('policy_fingerprint'), params['policy_fingerprint']))
        full = True

    # --- текущее состояние файлов: сначала только stat, crc — для изменённых ---
    t = time.time()
    cur = scan(data_dir, keys, args.jobs)
    log('скан Data: %d файлов, %s исходников, %.1f с (crc всех)'
        % (len(cur), human(sum(v[0] for v in cur.values())), time.time() - t))

    # --- применяем манифест: снимаем привязки удалённых, помечаем изменённые pile ---
    piles = man.piles
    dirty = set(p.id for p in piles) if full else set()
    for p in piles:
        kept = []
        for f in p.files:
            rel = f[0]
            c = cur.get(rel)
            if c is None:
                log('  %s: файл удалён из Data, привязку снимаем: %s' % (p.name or '?', rel))
                dirty.add(p.id)
                continue
            size, crc = c
            if size != f[1] or crc != f[2]:
                dirty.add(p.id)
            kept.append([rel, size, crc])
        p.files = kept
        if p.name is None:
            dirty.add(p.id)
        else:
            path = os.path.join(out_dir, p.name)
            if not os.path.isfile(path):
                dirty.add(p.id)
            elif not full and not disk_matches(p, path):
                log('  %s: индекс на диске не совпадает с манифестом — пересобираем' % p.name)
                dirty.add(p.id)
    piles = [p for p in piles if p.files]

    # --- новые файлы: в последний pile, не достигший target; иначе новый pile ---
    bound = set(f[0] for p in piles for f in p.files)
    new_keys = sorted(k for k in cur if k not in bound)
    if new_keys:
        target_pile = None
        for p in reversed(piles):
            if p.packed_size < args.target:
                target_pile = p
                break
        for k in new_keys:
            size = cur[k][0]
            if target_pile is not None and \
                    target_pile.packed_size + est_size(k, size, args.est_ratio) > args.target * 1.05:
                target_pile = None
            if target_pile is None:
                target_pile = Pile(man.next_id)
                man.next_id += 1
                piles.append(target_pile)
            target_pile.files.append([k, cur[k][0], cur[k][1]])
            target_pile.packed_size += est_size(k, size, args.est_ratio)
            dirty.add(target_pile.id)
        log('новых файлов: %d' % len(new_keys))

    piles = [p for p in piles if p.files]
    renumber(piles, args.prefix)

    # --- проверки лимитов загрузчика и целостности привязок ---
    owner = {}
    for p in piles:
        for f in p.files:
            lk = f[0].lower()
            if lk in owner:
                raise SystemExit('файл %s привязан к двум pile (%s, %s)' % (f[0], owner[lk], p.name))
            owner[lk] = p.name
        if len(p.files) >= LOADER_MAX_FILES_PER_PILE:
            raise SystemExit('%s: %d файлов >= лимита загрузчика' % (p.name, len(p.files)))
        if sum(len(f[0]) + 1 for f in p.files) >= LOADER_MAX_STRINGS:
            raise SystemExit('%s: таблица имён >= 16 МБ — лимит загрузчика' % p.name)
        if p.packed_size >= COMPILER_MAX_PILE_SIZE:
            raise SystemExit('%s: размер pile >= 2 ГБ — лимит компилятора' % p.name)
        for f in p.files:
            if f[1] >= LOADER_MAX_FILE_SIZE:
                raise SystemExit('%s: %s %s >= 256 МБ — лимит fs_piled_file_size_limit'
                                 % (p.name, f[0], human(f[1])))

    todo = [p for p in piles if p.id in dirty]
    log('')
    log('план: pile всего %d, пересобираем %d, без изменений %d'
        % (len(piles), len(todo), len(piles) - len(todo)))
    for p in piles:
        log('  %-8s %-18s %6d файлов  src %9s  packed %9s'
            % ('REBUILD' if p.id in dirty else 'keep', p.name, len(p.files),
               human(p.source_size()), human(p.packed_size)))
    if args.dry_run:
        log('')
        log('--dry-run: ничего не записано (манифест тоже не тронут)')
        return 0

    # --- сборка помеченных pile, затем деление переполненных и пересборка частей ---
    disk_names = {p.id: p.name for p in man_piles if p.name}
    sync_names(piles, disk_names, out_dir, args.prefix)
    rebuild(piles, dirty, data_dir, out_dir, args)
    refresh_disk_names(piles, disk_names, out_dir)
    guard = 0
    while True:
        # делить можно только pile с несколькими файлами: один файл крупнее порога
        # (голосовые банки по 60-80 МБ) остаётся в pile целиком — это не ошибка
        over = [p for p in piles if p.packed_size > args.split and len(p.files) > 1]
        if not over:
            break
        guard += 1
        if guard > 20:
            raise SystemExit('деление не сходится за 20 итераций — проверьте --target/--split')
        log('pile больше %s: %s -> делим списки по исходным байтам'
            % (human(args.split), ', '.join('%s(%s)' % (p.name, human(p.packed_size)) for p in over)))
        piles, need = split_oversized(piles, man, args.split)
        sync_names(piles, disk_names, out_dir, args.prefix)
        rebuild(piles, need, data_dir, out_dir, args)
        refresh_disk_names(piles, disk_names, out_dir)

    sync_names(piles, disk_names, out_dir, args.prefix)
    for p in piles:
        if p.packed_size > args.split and len(p.files) == 1:
            log('  %s: один файл %s — больше порога деления, так и оставляем'
                % (p.name, human(p.packed_size)))

    # --- осиротевшие контейнеры в выходной папке ---
    keep = set(p.name for p in piles)
    for fn in sorted(os.listdir(out_dir)):
        if fn.endswith('.pile') and fn not in keep:
            if args.keep_stale:
                log('  устаревший pile оставлен: %s' % fn)
            else:
                os.remove(os.path.join(out_dir, fn))
                log('  удалён устаревший pile: %s' % fn)

    man.piles = piles
    man.save(params)

    total_src = sum(p.source_size() for p in piles)
    total_packed = sum(p.packed_size for p in piles)
    log('')
    log('ИТОГО: %d pile, %d файлов, src %s -> packed %s (ratio %.3f), %.1f с'
        % (len(piles), sum(len(p.files) for p in piles), human(total_src), human(total_packed),
           total_packed / float(total_src) if total_src else 0.0, time.time() - t0))
    log('манифест: %s' % man.path)
    log('выход:    %s' % out_dir)
    return 0


# --------------------------------------------------------------------------------------
# verify
# --------------------------------------------------------------------------------------

def verify_pile(path, check_crc=True):
    problems = []
    try:
        size = os.path.getsize(path)
        if size < TAIL.size:
            return ['файл короче хвоста']
        with open(path, 'rb') as f:
            f.seek(-TAIL.size, os.SEEK_END)
            tag, version, nfiles, strings_off, strings_len, index_off = TAIL.unpack(f.read(TAIL.size))
            if tag != FILEPILE_TAG:
                problems.append('tail.tag=0x%08x != 0x%08x' % (tag, FILEPILE_TAG))
            if version != FILEPILE_VERSION:
                problems.append('tail.version=0x%08x != 0x%08x' % (version, FILEPILE_VERSION))
            if not 0 < nfiles < LOADER_MAX_FILES_PER_PILE:
                problems.append('filesNumber=%d вне диапазона загрузчика' % nfiles)
            if not 0 < strings_len < LOADER_MAX_STRINGS:
                problems.append('stringsLength=%d вне диапазона загрузчика' % strings_len)
            if index_off + nfiles * ENTRY.size + TAIL.size != size:
                problems.append('индекс/хвост не сходятся с размером: %d + %d*%d + %d != %d'
                                % (index_off, nfiles, ENTRY.size, TAIL.size, size))
            if problems:
                return problems

            f.seek(strings_off)
            strings, _ = crypt(f.read(strings_len))
            f.seek(index_off)
            raw_index, _ = crypt(f.read(nfiles * ENTRY.size))

            seen = {}
            entries = []
            for i in range(nfiles):
                esize, esrc, eoff, eflags, enoff, ecrc = ENTRY.unpack(
                    raw_index[i * ENTRY.size:(i + 1) * ENTRY.size])
                z = strings.find(b'\x00', enoff)
                name = strings[enoff:z]
                key = os.fsdecode(name)
                lk = key.lower()
                if lk in seen:
                    problems.append('дубликат ключа (case-insensitive): %s и %s' % (seen[lk], key))
                seen[lk] = key
                if not name or name[:1] in (b'0', b'/'):
                    problems.append('имя %r начинается с %r — загрузчик сочтёт pile битым'
                                    % (key, os.fsdecode(name[:1])))
                if esize >= LOADER_MAX_FILE_SIZE or esrc >= LOADER_MAX_FILE_SIZE:
                    problems.append('%s: size/sourceSize >= лимита загрузчика' % key)
                if eoff + esize > index_off:
                    problems.append('%s: данные вылезли за индекс' % key)
                if (eflags & FILEPILE_COMPRESSED) and esize == esrc:
                    problems.append('%s: флаг COMPRESSED, но size == sourceSize' % key)
                entries.append((key, esize, esrc, eoff, eflags, ecrc))

            if check_crc:
                for key, esize, esrc, eoff, eflags, ecrc in entries:
                    f.seek(eoff)
                    blob = f.read(esize)
                    if len(blob) != esize:
                        problems.append('%s: не дочитано %d байт' % (key, esize - len(blob)))
                        continue
                    if eflags & FILEPILE_COMPRESSED:
                        try:
                            data = zlib.decompress(blob)
                        except zlib.error as e:
                            problems.append('%s: zlib: %s' % (key, e))
                            continue
                        if len(data) != esrc:
                            problems.append('%s: sourceSize=%d, распаковалось %d' % (key, esrc, len(data)))
                            continue
                    else:
                        data = blob
                    if (zlib.crc32(data) & 0xFFFFFFFF) != ecrc:
                        problems.append('%s: crc не совпадает' % key)
    except Exception as e:                                     # noqa: BLE001
        problems.append('исключение: %r' % (e,))
    return problems


def cmd_verify(args):
    out_dir = os.path.abspath(args.out)
    if not os.path.isdir(out_dir):
        raise SystemExit('нет выходной папки: %s' % out_dir)
    files = sorted(f for f in os.listdir(out_dir) if f.endswith('.pile'))
    if not files:
        raise SystemExit('в %s нет pile-файлов' % out_dir)
    bad = 0
    for fn in files:
        path = os.path.join(out_dir, fn)
        probs = verify_pile(path, check_crc=not args.fast)
        nfiles = 0
        try:
            with open(path, 'rb') as f:
                f.seek(-TAIL.size, os.SEEK_END)
                nfiles = TAIL.unpack(f.read(TAIL.size))[2]
        except Exception:
            pass
        log('%-20s %6d файлов  %10s  %s'
            % (fn, nfiles, human(os.path.getsize(path)), 'OK' if not probs else 'ОШИБКИ'))
        for p in probs:
            log('    ! %s' % p)
        if probs:
            bad += 1
    log('')
    log('проверено %d pile, с проблемами %d' % (len(files), bad))

    # сверка с манифестом: набор файлов на диске == набор из манифеста, индексы
    # совпадают, каждый ключ встречается ровно один раз
    man = Manifest(os.path.abspath(args.manifest))
    if os.path.isfile(man.path):
        man.load()
        want = {p.name for p in man.piles if p.name}
        have = set(files)
        for name in sorted(have - want):
            log('  ! %s есть в %s, но его нет в манифесте' % (name, out_dir))
            bad += 1
        for name in sorted(want - have):
            log('  ! %s значится в манифесте, но отсутствует в %s' % (name, out_dir))
            bad += 1
        seen = {}
        for p in man.piles:
            for f in p.files:
                lk = f[0].lower()
                if lk in seen:
                    log('  ! %s закреплён за двумя pile: %s и %s'
                        % (f[0], seen[lk], p.name))
                    bad += 1
                seen[lk] = p.name
            if p.name in have and not disk_matches(p, os.path.join(out_dir, p.name)):
                log('  ! %s: индекс не совпадает с манифестом' % p.name)
                bad += 1
        log('манифест: %d pile, %d ключей; сверка %s'
            % (len(man.piles), len(seen), 'чисто' if not bad else 'с проблемами'))
    else:
        log('манифеста нет (%s) — сверка состава пропущена' % man.path)
    return 1 if bad else 0


# --------------------------------------------------------------------------------------
# extract: быстрый доступ к содержимому готового pile (сверка, отладка клиента)
# --------------------------------------------------------------------------------------

def pile_entries(path):
    """[(key, size, source_size, offset, flags, crc)] по собранному pile."""
    with open(path, 'rb') as f:
        f.seek(-TAIL.size, os.SEEK_END)
        tag, version, nfiles, strings_off, strings_len, index_off = TAIL.unpack(f.read(TAIL.size))
        if tag != FILEPILE_TAG or version != FILEPILE_VERSION:
            raise SystemExit('%s: tag/version не совпадают (0x%08x / 0x%08x)'
                             % (path, tag, version))
        f.seek(strings_off)
        strings, _ = crypt(f.read(strings_len))
        f.seek(index_off)
        raw_index, _ = crypt(f.read(nfiles * ENTRY.size))
        out = []
        for i in range(nfiles):
            esize, esrc, eoff, eflags, enoff, ecrc = ENTRY.unpack(
                raw_index[i * ENTRY.size:(i + 1) * ENTRY.size])
            z = strings.find(b'\x00', enoff)
            out.append((os.fsdecode(strings[enoff:z]), esize, esrc, eoff, eflags, ecrc))
        return out


def cmd_extract(args):
    entries = pile_entries(args.pile)
    if not args.keys:
        for key, esize, esrc, eoff, eflags, ecrc in entries:
            log('%-70s %10d -> %10d  %s  crc=%08x'
                % (key, esrc, esize, 'zlib' if eflags & FILEPILE_COMPRESSED else 'raw ', ecrc))
        log('')
        log('%d записей' % len(entries))
        return 0
    by_key = {e[0].lower(): e for e in entries}
    for want in args.keys:
        e = by_key.get(want.lower())
        if e is None:
            log('нет записи %r (первое имя: %s)' % (want, entries[0][0] if entries else '-'))
            return 1
        key, esize, esrc, eoff, eflags, ecrc = e
        with open(args.pile, 'rb') as f:
            f.seek(eoff)
            blob = f.read(esize)
        data = zlib.decompress(blob) if eflags & FILEPILE_COMPRESSED else blob
        if (zlib.crc32(data) & 0xFFFFFFFF) != ecrc:
            log('%s: ВНИМАНИЕ, crc не совпадает' % key)
        if args.to in (None, '-'):
            sys.stdout.buffer.write(data)
        else:
            with open(args.to, 'wb') as f:
                f.write(data)
            log('%s -> %s (%d байт)' % (key, args.to, len(data)))
    return 0


def cmd_status(args):
    man = Manifest(args.manifest)
    man.load()
    if not man.piles:
        log('манифест пуст (первый прогон раскладывает всё с нуля): %s' % man.path)
        return 0
    log('манифест: %s' % man.path)
    log('params: %s' % json.dumps(man.params, sort_keys=True))
    for p in man.piles:
        log('  %-18s %6d файлов  src %9s  packed %9s'
            % (p.name, len(p.files), human(p.source_size()), human(p.packed_size)))
    log('итого: %d pile, %d файлов, src %s, packed %s'
        % (len(man.piles), sum(len(p.files) for p in man.piles),
           human(sum(p.source_size() for p in man.piles)),
           human(sum(p.packed_size for p in man.piles))))
    return 0


def cmd_clean(args):
    out_dir = os.path.abspath(args.out)
    if not os.path.isdir(out_dir):
        return 0
    for fn in sorted(os.listdir(out_dir)):
        if fn.endswith('.pile') or fn.endswith('.pile.tmp') or fn.startswith('.renam'):
            os.remove(os.path.join(out_dir, fn))
            log('удалён %s' % fn)
    return 0


# --------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------

def parse_size(s):
    m = re.match(r'^(\d+(?:\.\d+)?)\s*([km]b)?$', str(s).strip().lower())
    if not m:
        raise argparse.ArgumentTypeError('размер: число байт или с суффиксом, напр. 50MB / 52428800')
    v = float(m.group(1))
    unit = m.group(2)
    if unit == 'kb':
        v *= 1024
    elif unit == 'mb':
        v *= MB
    return int(v)


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    known = ('build', 'plan', 'verify', 'status', 'clean', 'extract')
    if not argv or argv[0] not in known:
        argv = ['build'] + argv          # ./pilepack.sh --data … = ./pilepack.sh build --data …
    ap = argparse.ArgumentParser(
        description='Сборка pile-контейнеров gamedataNN.pile из дерева Data.')
    sub = ap.add_subparsers(dest='cmd')

    def paths(p):
        p.add_argument('--data', default=DEFAULT_DATA, help='дерево Data (по умолчанию %(default)s)')
        p.add_argument('--out', default=DEFAULT_OUT, help='куда писать pile (по умолчанию %(default)s)')
        p.add_argument('--policy', default=DEFAULT_POLICY, help='политика отбора (по умолчанию %(default)s)')
        p.add_argument('--manifest', default=DEFAULT_MANIFEST, help='файл привязок (по умолчанию %(default)s)')

    def common(p):
        paths(p)
        p.add_argument('--target', type=parse_size, default=DEFAULT_TARGET,
                       help='целевой размер pile (по умолчанию 50MB)')
        p.add_argument('--split', type=parse_size, default=DEFAULT_SPLIT,
                       help='порог деления pile (по умолчанию 60MB)')
        p.add_argument('--level', type=int, default=DEFAULT_LEVEL, help='zlib level (по умолчанию 6)')
        p.add_argument('--ratio', type=float, default=DEFAULT_RATIO,
                       help='макс. ratio для применения сжатия (по умолчанию 0.90)')
        p.add_argument('--min-cs', type=int, default=DEFAULT_MIN_CS,
                       help='файлы больше этого размера пробуют сжимать (по умолчанию 1)')
        p.add_argument('--est-ratio', type=float, default=DEFAULT_EST_RATIO,
                       help='оценка ratio при первой раскладке (по умолчанию 0.50)')
        p.add_argument('--prefix', default=DEFAULT_PREFIX, help='префикс имён pile (по умолчанию gamedata)')
        p.add_argument('-j', '--jobs', type=int,
                       default=max(1, min(4, (os.cpu_count() or 2) - 1)),
                       help='параллельные воркеры (по умолчанию %(default)s)')
        p.add_argument('--force', action='store_true', help='пересобрать все pile')
        p.add_argument('--keep-stale', action='store_true',
                       help='не удалять pile, которых нет в манифесте')

    p_build = sub.add_parser('build', help='собрать/дообновить pile')
    common(p_build)
    p_build.add_argument('--dry-run', action='store_true', help='только план, ничего не писать')
    p_plan = sub.add_parser('plan', help='то же, что build --dry-run')
    common(p_plan)
    p_verify = sub.add_parser('verify', help='проверить собранные pile')
    paths(p_verify)
    p_verify.add_argument('--fast', action='store_true', help='не проверять crc содержимого')
    p_status = sub.add_parser('status', help='сводка по манифесту')
    paths(p_status)
    p_clean = sub.add_parser('clean', help='удалить pile из выходной папки')
    p_clean.add_argument('--out', default=DEFAULT_OUT, help='что чистить (по умолчанию %(default)s)')
    p_extract = sub.add_parser('extract', help='список записей pile или выгрузка файла')
    p_extract.add_argument('pile', help='файл pile')
    p_extract.add_argument('keys', nargs='*', help='ключ(и) записи (без учёта регистра)')
    p_extract.add_argument('--to', default='-', help='куда писать ("-" = stdout)')

    args = ap.parse_args(argv)
    if args.cmd == 'plan':
        args.dry_run = True
    if args.cmd in ('build', 'plan'):
        return cmd_build(args)
    if args.cmd == 'verify':
        return cmd_verify(args)
    if args.cmd == 'status':
        return cmd_status(args)
    if args.cmd == 'extract':
        return cmd_extract(args)
    if args.cmd == 'clean':
        return cmd_clean(args)
    ap.print_help()
    return 2


if __name__ == '__main__':
    sys.exit(main())
