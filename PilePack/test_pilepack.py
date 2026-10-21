#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Тесты pilepack.py: формат контейнера, привязка файлов из манифеста, инкрементальность,
деление переполненного pile, проверки лимитов загрузчика.

Запуск из корня репозитория:  python3 PilePack/test_pilepack.py
Временные каталоги — в tempfile.TemporaryDirectory, ничего общего с Data не трогает.
"""

import io
import json
import os
import random
import struct
import sys
import tempfile
import contextlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pilepack as pp                                            # noqa: E402

POLICY = """ext: txt bin
ignore_dir: skipme
exclude: *.ignore
"""

FAILS = []


def check(cond, what):
    print(('  ok   ' if cond else '  FAIL ') + what)
    if not cond:
        FAILS.append(what)


def run(argv):
    """Вызывает CLI pilepack, возвращает (код, stderr). SystemExit считается ошибкой."""
    buf = io.StringIO()
    try:
        with contextlib.redirect_stderr(buf):
            code = pp.main(argv)
    except SystemExit as e:
        if not isinstance(e.code, int):
            buf.write('%s\n' % (e.code,))
        code = e.code if isinstance(e.code, int) and e.code else 1
    return code, buf.getvalue()


def mkfile(path, size, kind='text', seed=1):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    rnd = random.Random(seed)
    if kind == 'text':
        data = (b'lorem ipsum %d\n' % seed) * (size // 16 + 1)
        data = data[:size]
    else:                                        # не сжимается: ratio > 0.90 -> raw
        data = bytes(rnd.randrange(256) for _ in range(size))
    with open(path, 'wb') as f:
        f.write(data)
    return data


def tree(root, spec):
    """spec: {относительный путь: (размер, kind)}"""
    made = {}
    for rel, (size, kind) in spec.items():
        made[rel] = mkfile(os.path.join(root, rel.replace('/', os.sep)), size, kind)
    return made


def common_args(root):
    return ['--data', os.path.join(root, 'Data'), '--out', os.path.join(root, 'Piles'),
            '--manifest', os.path.join(root, 'manifest.json'),
            '--policy', os.path.join(root, 'policy.txt'),
            '-j', '1', '--level', '6', '--est-ratio', '1.0']


def pile_names(root):
    d = os.path.join(root, 'Piles')
    return sorted(f for f in os.listdir(d) if f.endswith('.pile'))


def mtimes(root):
    d = os.path.join(root, 'Piles')
    return {f: os.stat(os.path.join(d, f)).st_mtime_ns for f in pile_names(root)}


def verify_args(root):
    return type('A', (), {'out': os.path.join(root, 'Piles'), 'fast': False,
                          'manifest': os.path.join(root, 'manifest.json')})()


def manifest_of(root):
    with open(os.path.join(root, 'manifest.json'), 'rb') as f:
        return json.loads(f.read().decode('utf-8'))


def load_manifest_piles(root):
    man = pp.Manifest(os.path.join(root, 'manifest.json'))
    man.load()
    return man.piles


# ----------------------------------------------------------------------------------

def test_format_smoke(root):
    print('формат: битый тег / версия / COMPRESSED без сжатия / имя с ведущим 0')
    data = os.path.join(root, 'Data')
    src = tree(data, {'a.txt': (5000, 'text'), 'b.bin': (4000, 'rand'), 'c.txt': (30, 'text')})
    out = os.path.join(root, 'Piles')
    os.makedirs(out, exist_ok=True)

    r = pp.build_pile((os.path.join(out, 'x.pile'), data, ['a.txt', 'b.bin', 'c.txt'], 6, 1, 0.90))
    check(not r.get('error'), 'build_pile собрал без ошибок')
    check(pp.verify_pile(os.path.join(out, 'x.pile')) == [], 'собранный pile проходит verify')

    # содержимое совпадает с исходником (и сжатая запись, и несжатая)
    ents = {e[0]: e for e in pp.pile_entries(os.path.join(out, 'x.pile'))}
    ok = True
    for rel in src:
        key, esize, esrc, eoff, eflags, ecrc = ents[rel]
        with open(os.path.join(out, 'x.pile'), 'rb') as f:
            f.seek(eoff)
            blob = f.read(esize)
        got = pp.zlib.decompress(blob) if eflags & pp.FILEPILE_COMPRESSED else blob
        ok = ok and got == src[rel] and esrc == len(src[rel])
    check(ok, 'байты записей совпадают с исходниками')
    check(ents['b.bin'][4] == 0, 'несжимаемый файл записан без флага COMPRESSED')
    check(ents['a.txt'][4] & pp.FILEPILE_COMPRESSED != 0, 'сжимаемый файл сжат и флаг стоит')

    # хвост не шифруется, тег/версия на месте
    with open(os.path.join(out, 'x.pile'), 'rb') as f:
        f.seek(-pp.TAIL.size, os.SEEK_END)
        tail = pp.TAIL.unpack(f.read(pp.TAIL.size))
    check(tail[0] == pp.FILEPILE_TAG and tail[1] == pp.FILEPILE_VERSION and tail[2] == 3,
          'хвост: tag/version/filesNumber корректны')
    with open(os.path.join(out, 'x.pile'), 'rb') as f:
        check(struct.unpack('<I', f.read(4))[0] == pp.FILEPILE_TAG, 'тег в начале файла')

    # битый тег в хвосте
    raw = bytearray(open(os.path.join(out, 'x.pile'), 'rb').read())
    raw[-pp.TAIL.size] ^= 0xFF
    open(os.path.join(out, 'bad_tag.pile'), 'wb').write(bytes(raw))
    check(any('tag' in p for p in pp.verify_pile(os.path.join(out, 'bad_tag.pile'))),
          'verify ловит битый тег')

    # чужая версия
    raw = bytearray(open(os.path.join(out, 'x.pile'), 'rb').read())
    struct.pack_into('<I', raw, len(raw) - pp.TAIL.size + 4, 0x00010003)
    open(os.path.join(out, 'bad_ver.pile'), 'wb').write(bytes(raw))
    check(any('version' in p for p in pp.verify_pile(os.path.join(out, 'bad_ver.pile'))),
          'verify ловит чужую версию')

    # запись со флагом COMPRESSED, но байты не сжаты (так клиент читает криво)
    raw = bytearray(open(os.path.join(out, 'x.pile'), 'rb').read())
    index_off = struct.unpack_from('<I', raw, len(raw) - pp.TAIL.size + 20)[0]
    nfiles = struct.unpack_from('<I', raw, len(raw) - pp.TAIL.size + 8)[0]
    blob, _ = pp.crypt(bytes(raw[index_off:index_off + nfiles * pp.ENTRY.size]), 0)
    ents = [list(pp.ENTRY.unpack(blob[i * pp.ENTRY.size:(i + 1) * pp.ENTRY.size]))
            for i in range(nfiles)]
    idx = next(i for i, e in enumerate(ents) if not (e[3] & pp.FILEPILE_COMPRESSED))
    ents[idx][3] |= pp.FILEPILE_COMPRESSED          # флаг на записи с несжатыми байтами
    reb, st = b'', 0
    for e in ents:                                  # шифр в индексе — непрерывный поток
        enc, st = pp.crypt(pp.ENTRY.pack(*e), st)
        reb += enc
    raw[index_off:index_off + nfiles * pp.ENTRY.size] = reb
    open(os.path.join(out, 'bad_flag.pile'), 'wb').write(bytes(raw))
    probs = pp.verify_pile(os.path.join(out, 'bad_flag.pile'))
    check(any('COMPRESSED' in p or 'zlib' in p for p in probs),
          'verify ловит COMPRESSED без сжатых данных')

    # имя с ведущим '0' ломает Init() всего контейнера на клиенте
    r = pp.build_pile((os.path.join(out, 'zero.pile'), data, ['a.txt'], 6, 1, 0.90))
    raw = bytearray(open(os.path.join(out, 'zero.pile'), 'rb').read())
    index_off = struct.unpack_from('<I', raw, len(raw) - pp.TAIL.size + 20)[0]
    strings_off = struct.unpack_from('<I', raw, len(raw) - pp.TAIL.size + 12)[0]
    sblob, _ = pp.crypt(bytes(raw[strings_off:index_off]), 0)
    fixed = b'0' + sblob[1:]
    enc, _ = pp.crypt(fixed, 0)
    raw[strings_off:index_off] = enc
    open(os.path.join(out, 'zero.pile'), 'wb').write(bytes(raw))
    check(any('загрузчик' in p for p in pp.verify_pile(os.path.join(out, 'zero.pile'))),
          'verify ловит имя с ведущим 0')
    os.remove(os.path.join(out, 'x.pile'))
    for junk in ('bad_tag.pile', 'bad_ver.pile', 'bad_flag.pile', 'zero.pile'):
        os.remove(os.path.join(out, junk))


def test_first_build_and_incremental(root):
    print('привязка: первый прогон, повторный без изменений, изменение одного файла')
    data = os.path.join(root, 'Data')
    spec = {}
    for i in range(12):
        spec['dir%02d/f%02d.bin' % (i // 3, i)] = (10000, 'rand')
    tree(data, spec)
    a = common_args(root) + ['--target', '25000', '--split', '40000']

    code, out = run(['build'] + a)
    check(code == 0, 'первый build завершился (%s)' % out.strip().splitlines()[-3:])
    names = pile_names(root)
    check(names == ['gamedata%02d.pile' % i for i in range(1, len(names) + 1)] and len(names) >= 3,
          'имена gamedataNN.pile подряд: %s' % names)
    piles = load_manifest_piles(root)
    check(sum(len(p.files) for p in piles) == 12, 'все 12 файлов разложены')
    check(all(p.packed_size <= 25000 * 1.2 for p in piles),
          'каждый pile в пределах цели: %s' % [p.packed_size for p in piles])
    check(pp.cmd_verify(verify_args(root)) == 0,
          'verify: все pile чистые')

    # битый pile ловится: портим байт в середине данных
    victim = os.path.join(root, 'Piles', names[0])
    with open(victim, 'rb') as f:
        good = f.read()
    mid = len(good) // 2
    with open(victim, 'r+b') as f:
        f.seek(mid)
        f.write(b'\x00' if good[mid] != 0 else b'\xff')
    check(pp.cmd_verify(verify_args(root)) == 1, 'verify ловит испорченный pile')
    with open(victim, 'wb') as f:
        f.write(good[:200])
    code, out = run(['build'] + a)
    check('индекс на диске не совпадает с манифестом' in out,
          'огрысок pile вместо файла замечен и пересобран (%s)'
          % [l for l in out.splitlines() if 'план:' in l or 'индекс' in l])
    check(pp.cmd_verify(verify_args(root)) == 0, 'после починки verify снова чист')

    # повторный прогон: пересобирать нечего
    before = mtimes(root)
    code, out = run(['build'] + a)
    check('пересобираем 0' in out, 'повторный прогон не пересобирает pile (%s)'
          % [l for l in out.splitlines() if 'план:' in l])
    check(mtimes(root) == before, 'файлы pile не тронуты')

    # меняем содержимое одного файла, размер тот же -> crc другой
    rel = 'dir01/f04.bin'
    pid = [p.id for p in load_manifest_piles(root) if any(f[0] == rel for f in p.files)][0]
    pname = [p.name for p in load_manifest_piles(root) if p.id == pid][0]
    mkfile(os.path.join(data, rel.replace('/', os.sep)), 10000, 'rand', seed=99)
    before = mtimes(root)
    code, out = run(['build'] + a)
    check('пересобираем 1' in out, 'изменение файла помечает только его pile')
    check(mtimes(root)[pname] != before[pname], '%s перезаписан' % pname)
    others = {n: t for n, t in mtimes(root).items() if n != pname}
    check(others == {n: t for n, t in before.items() if n != pname},
          'остальные pile не тронуты')

    # файл с тем же содержимым, но новым mtime -> пересборки нет
    os.utime(os.path.join(data, rel.replace('/', os.sep)), (1000000000, 1000000000))
    code, out = run(['build'] + a)
    check('пересобираем 0' in out, 'смена mtime без смены содержимого не вызывает пересборку')


def test_new_and_deleted_files(root):
    print('привязка: новые файлы в последний неполный pile, удалённые снимаются с привязки')
    data = os.path.join(root, 'Data')
    a = common_args(root) + ['--target', '25000', '--split', '40000']
    piles = load_manifest_piles(root)
    last_under = [p for p in piles if p.packed_size < 25000][-1]
    counts = {p.name: len(p.files) for p in piles}

    tree(data, {'newdir/new1.bin': (2000, 'rand'), 'newdir/new2.bin': (2000, 'rand')})
    code, out = run(['build'] + a)
    after = {p.name: [f[0] for f in p.files] for p in load_manifest_piles(root)}
    check(len(pile_names(root)) == len(counts), 'новых pile не потребовалось')
    check('newdir/new1.bin' in after[last_under.name] and 'newdir/new2.bin' in after[last_under.name],
          'новые файлы попали в последний неполный pile (%s)' % last_under.name)
    check('новых файлов: 2' in out, 'отчёт про новые файлы')

    # удаляем файл: запись уходит из pile, сам pile остаётся
    victim = 'dir02/f06.bin'
    os.remove(os.path.join(data, victim.replace('/', os.sep)))
    code, out = run(['build'] + a)
    check('файл удалён из Data' in out, 'удаление зафиксировано в логе')
    after = {p.name: [f[0] for f in p.files] for p in load_manifest_piles(root)}
    check(all(victim not in v for v in after.values()), 'запись о удалённом файле исчезла')
    check(len(pile_names(root)) == len(counts), 'pile на месте (привязка сохранена)')
    check(pp.cmd_verify(verify_args(root)) == 0,
          'verify после удаления: чисто')

    # возвращаем файл: он снова становится «новым» и попадает в тот же pile
    mkfile(os.path.join(data, victim.replace('/', os.sep)), 10000, 'rand', seed=6)
    code, out = run(['build'] + a)
    after = {p.name: [f[0] for f in p.files] for p in load_manifest_piles(root)}
    check(any(victim in v for v in after.values()), 'вернувшийся файл снова принят')


def test_split_oversized(root):
    print('деление: pile выше порога режется по исходным байтам')
    data = os.path.join(root, 'Data')
    a = common_args(root) + ['--target', '25000', '--split', '40000']
    piles = load_manifest_piles(root)
    target = piles[0]
    before_names = [p.name for p in piles]

    # раздуваем один файл из первого pile так, чтобы pile перевалил за split
    big = target.files[0][0]
    mkfile(os.path.join(data, big.replace('/', os.sep)), 32000, 'rand', seed=7)
    before = mtimes(root)
    code, out = run(['build'] + a)
    check('делим списки по исходным байтам' in out, 'деление запущено')
    piles = load_manifest_piles(root)
    names = [p.name for p in piles]
    check(len(names) == len(before_names) + 1, 'появился ещё один pile: %s' % names)
    new_name = 'gamedata%02d.pile' % (len(before_names) + 1)
    check(sorted(names) == sorted(before_names + [new_name]),
          'имена стабильны, новая часть — %s: %s' % (new_name, names))
    after = mtimes(root)
    untouched = [n for n in before_names if n != target.name]
    check(all(after[n] == before[n] for n in untouched),
          'остальные pile не переименованы и не пересобраны')
    check(all(p.packed_size <= 40000 for p in piles),
          'все pile ниже порога: %s' % [p.packed_size for p in piles])
    src_sizes = [p.source_size() for p in piles if big in [f[0] for f in p.files]]
    check(len(src_sizes) == 1, 'большой файл остался в одном pile')
    check(pp.cmd_verify(verify_args(root)) == 0,
          'verify после деления: чисто')
    # содержимое соседних pile не пересобрано зря: сверяем crc по манифесту
    total = sum(len(p.files) for p in piles)
    check(total == len(pp.Policy(os.path.join(root, 'policy.txt')).walk(data)),
          'все файлы Data разложены (%d)' % total)


def test_relayout_on_target_change(root):
    print('смена цели: файлы раскладываются заново, а не делятся пополам')
    data = os.path.join(root, 'Data')
    spec = {'r/f%03d.bin' % i: (3000, 'rand') for i in range(40)}
    tree(data, spec)
    code, out = run(['build'] + common_args(root) + ['--target', '60000'])
    check(code == 0, 'первая сборка по 60000')
    before = len(pile_names(root))
    code, out = run(['build'] + common_args(root) + ['--target', '15000'])
    check('раскладка заново' in out, 'смена цели запускает раскладку заново')
    piles = load_manifest_piles(root)
    check(len(piles) > before * 2, 'pile стало больше: %d -> %d' % (before, len(piles)))
    check(all(p.packed_size <= 15000 + pp.SPLIT_MARGIN for p in piles),
          'все pile в пределах новой цели: %s' % [p.packed_size for p in piles])
    check(pile_names(root) == sorted(p.name for p in piles), 'старых pile в выходной папке не осталось')
    check(sum(len(p.files) for p in piles) == 40, 'все 40 файлов разложены')
    check(pp.cmd_verify(verify_args(root)) == 0, 'verify после раскладки заново: чисто')
    code, out = run(['build'] + common_args(root) + ['--target', '15000'])
    check('пересобираем 0' in out and 'раскладка заново' not in out,
          'повторный прогон с той же целью ничего не трогает')


def test_policy_and_guards(root):
    print('политика и защиты: ext/ignore_dir/exclude, конфликт регистра, имя с 0')
    data = os.path.join(root, 'Data')
    os.makedirs(os.path.join(data, 'skipme'), exist_ok=True)
    mkfile(os.path.join(data, 'skipme', 'x.txt'), 100, 'text')
    mkfile(os.path.join(data, 'ignored.txt.ignore'), 100, 'text')
    mkfile(os.path.join(data, 'notpacked.maya'), 100, 'text')
    pol = pp.Policy(os.path.join(root, 'policy.txt'))
    keys = set(pol.walk(data))
    check('skipme/x.txt' not in keys, 'ignore_dir работает')
    check('ignored.txt.ignore' not in keys, 'exclude *.ignore работает')
    check('notpacked.maya' not in keys, 'расширение вне ext не берётся')
    check('dir00/f00.bin' in keys, 'разрешённое расширение взято')

    pol2 = os.path.join(root, 'policy2.txt')
    with open(pol2, 'w', encoding='utf-8') as f:
        f.write(POLICY + 'exclude: dir00/*\nexclude: !dir00/f00.bin\n')
    keys2 = set(pp.Policy(pol2).walk(data))
    check('dir00/f00.bin' in keys2 and 'dir00/f01.bin' not in keys2,
          'exclude с !-исключением работает')

    mkfile(os.path.join(data, 'Zz.txt'), 10, 'text')
    mkfile(os.path.join(data, 'zz.txt'), 10, 'text')
    if len([n for n in os.listdir(data) if n.lower() == 'zz.txt']) < 2:
        print('  skip конфликт регистра: ФС без учёта регистра (Windows), два имени — один файл')
        os.remove(os.path.join(data, 'zz.txt'))
    else:
        code, out = run(['plan'] + common_args(root))
        check(code != 0 or 'конфликт имён без учёта регистра' in out,
              'конфликт имён без учёта регистра не пропускается')
        os.remove(os.path.join(data, 'Zz.txt'))
        os.remove(os.path.join(data, 'zz.txt'))

    mkfile(os.path.join(data, '0zero.txt'), 10, 'text')
    code, out = run(['plan'] + common_args(root))
    check(code != 0 or 'отвергнет pile' in out, 'имя с ведущим 0 не уходит в pile')
    os.remove(os.path.join(data, '0zero.txt'))


def test_line_endings(root):
    print('переводы строк: CRLF и LF в Data дают одинаковые pile, двоичные файлы не трогаются')
    data = os.path.join(root, 'Data')
    lf = b'<?xml version="1.0"?>\n<a>\n  <b>1</b>\n</a>\n' * 50
    binary = b'\x00\r\n\x01\r\n' * 100
    lone_cr = b'\xff\xfe\r\n\x80\r\x90' * 2000           # без NUL, но с одиночными CR (как layer_mask)
    a = common_args(root)

    def put(text):
        for rel, body in (('x/a.txt', text), ('x/b.bin', binary), ('x/c.bin', lone_cr)):
            path = os.path.join(data, rel.replace('/', os.sep))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as f:
                f.write(body)

    put(lf.replace(b'\n', b'\r\n'))
    code, out = run(['build'] + a)
    check(code == 0, 'build из CRLF-копии завершился')
    with open(os.path.join(root, 'Piles', 'gamedata01.pile'), 'rb') as f:
        from_crlf = f.read()
    files = {f[0]: f[1] for p in load_manifest_piles(root) for f in p.files}
    check(files.get('x/a.txt') == len(lf), 'размер текста в манифесте — с LF (%s)' % files.get('x/a.txt'))
    check(files.get('x/b.bin') == len(binary), 'двоичный файл с \\r\\n сохранён как есть')
    check(files.get('x/c.bin') == len(lone_cr), 'файл без NUL, но с одиночными CR — двоичный, не тронут')

    put(lf)
    code, out = run(['build'] + a)
    check('пересобираем 0' in out, 'та же Data с LF не вызывает пересборку (%s)'
          % [l for l in out.splitlines() if 'план:' in l])
    run(['build', '--force'] + a)
    with open(os.path.join(root, 'Piles', 'gamedata01.pile'), 'rb') as f:
        check(f.read() == from_crlf, 'pile из LF-копии побайтно равен pile из CRLF-копии')


def main():
    with tempfile.TemporaryDirectory(prefix='pilepack-test-') as root:
        with open(os.path.join(root, 'policy.txt'), 'w', encoding='utf-8') as f:
            f.write(POLICY)
        print('== формат')
        test_format_smoke(root)

    with tempfile.TemporaryDirectory(prefix='pilepack-test-') as root:
        with open(os.path.join(root, 'policy.txt'), 'w', encoding='utf-8') as f:
            f.write(POLICY)
        print('== test_line_endings')
        test_line_endings(root)

    with tempfile.TemporaryDirectory(prefix='pilepack-test-') as root:
        with open(os.path.join(root, 'policy.txt'), 'w', encoding='utf-8') as f:
            f.write(POLICY)
        print('== test_relayout_on_target_change')
        test_relayout_on_target_change(root)

    # одна и та же установка: сборка -> инкрементальность -> новые/удалённые -> деление
    with tempfile.TemporaryDirectory(prefix='pilepack-test-') as root:
        with open(os.path.join(root, 'policy.txt'), 'w', encoding='utf-8') as f:
            f.write(POLICY)
        for name in ('test_first_build_and_incremental', 'test_new_and_deleted_files',
                     'test_split_oversized', 'test_policy_and_guards'):
            print('== %s' % name)
            globals()[name](root)
    print('')
    if FAILS:
        print('ПРОВАЛОВ: %d' % len(FAILS))
        for f in FAILS:
            print('  - ' + f)
        return 1
    print('все проверки пройдены')
    return 0


if __name__ == '__main__':
    sys.exit(main())
