#!/usr/bin/env python3
"""Метрики снимка кадра (BMP/PNG) и сравнение с эталоном.

Зачем: глазком «чёрный/не чёрный» различать нельзя — в прогонах Vulkan-бэкенда
кадр выглядел «сценой», но оказывался 99% чёрным (насыщение NaN/огромных
значений в UNORM даёт ровно 0). Скрипт даёт числа: сколько пикселей чёрных,
какая средняя светимость, сколько уникальных цветов, и (при эталоне) насколько
кадр близок к DXVK-картинке.

Эталон: tools/render_shot/refs/wine-dxvk-2026-10-15/pw_auto_bb_3000.png
(полный кадр 1024x768, DXVK-путь того же клиента).

Использование:
  shot_stats.py КАДР.bmp [ЭТАЛОН.png]
  shot_stats.py --tone КАДР.bmp [КАДР2.bmp ...]
     метрика ОТТЕНКА (не светимости): гистограмма тона (12 бинов + «серый»),
     средняя насыщенность, среднее B-R. При двух кадрах — L1-расхождение
     гистограмм (0 = распределения совпали, 200 = не пересекаются).
     Зачем: «тёмная бочка с пурпурной крышкой» против «сплошной пурпурный
     цилиндр» дают почти равную среднюю светимость (|Δ| по средней 4,1 —
     гейт её не замечает), а по тону это разные распределения.
  shot_stats.py --lines КАДР.bmp [КАДР2.bmp ...]
     детектор тонких горизонтальных ЛИНИЙ (дефект Vulkan-кадра 2026-10-21):
     профиль средней светимости по строкам внутри узких вертикальных полос
     (по 192 px) минус скользящая медиана по 15 строкам. Глобальный профиль
     по всему кадру эти линии не видит (полупрозрачные, тонкие — см. раздел
     «A/B двух бэкендов» в плане), локальный — видит.
  shot_stats.py --defects КАДР.bmp [КАДР2.bmp ...]
     метрики дефектов, НЕ требующие сопоставленного кадра: сколько пикселей
     чисто белых / пурпурных (признаки подстановки несвязанной текстуры) и
     площадь крупнейшего белого прямоугольника. Сравнение «кадр vs эталон"
     на разных моментах сцены бессмысленно (см. PLAN_NVRHI, раздел про гейт
     паритета), а эти числа сравниваются между прогонами.
"""
import subprocess
import sys
import tempfile

import numpy as np


def load(path):
    """Любой BMP/PNG -> (H, W, 3) uint8 в порядке RGB."""
    if path.lower().endswith('.bmp'):
        d = open(path, 'rb').read()
        off = int.from_bytes(d[10:14], 'little')
        w = int.from_bytes(d[18:22], 'little')
        h = int.from_bytes(d[22:26], 'little')
        if h > 0x7FFFFFFF:
            h -= 1 << 32          # отрицательная высота = top-down
        bpp = int.from_bytes(d[28:30], 'little')
        if bpp != 24:
            raise SystemExit('ожидается 24-битный BMP, получен bpp=%d' % bpp)
        row = ((w * 3 + 3) // 4) * 4
        a = np.frombuffer(d, dtype=np.uint8)
        img = np.empty((abs(h), w, 3), dtype=np.uint8)
        for y in range(abs(h)):
            base = off + y * row
            img[abs(h) - 1 - y if h > 0 else y] = a[base:base + w * 3].reshape(w, 3)[:, ::-1]
        return img
    with tempfile.NamedTemporaryFile(suffix='.rgb', delete=False) as f:
        tmp = f.name
    subprocess.run(['convert', path, '-depth', '8', 'rgb:' + tmp], check=True)
    raw = open(tmp, 'rb').read()
    size = subprocess.run(['identify', '-format', '%w %h', path],
                          capture_output=True, text=True).stdout.split()
    w, h = int(size[0]), int(size[1])
    return np.frombuffer(raw, dtype=np.uint8).reshape(h, w, 3)


def stats(img, tag):
    h, w, _ = img.shape
    flat = img.reshape(-1, 3)
    lum = (0.299 * flat[:, 0] + 0.587 * flat[:, 1] + 0.114 * flat[:, 2])
    black = int(np.all(flat == 0, axis=1).sum())
    uniq = len(np.unique(flat, axis=0))
    print('%s: %dx%d  чёрных %d (%.2f%%)  средних %d  уникальных цветов %d'
          % (tag, w, h, black, 100.0 * black / flat.shape[0],
             int((lum > 8).sum()), uniq))
    # доминирующие цвета
    view = np.ascontiguousarray(flat).view(np.dtype((np.void, flat.dtype.itemsize * 3)))
    uq, cnt = np.unique(view, return_counts=True)
    colors = np.frombuffer(uq.tobytes(), dtype=np.uint8).reshape(-1, 3)
    for i in np.argsort(-cnt)[:6]:
        c = colors[i]
        print('    RGB(%3d,%3d,%3d) x %d' % (c[0], c[1], c[2], cnt[i]))
    return lum


TONE_BINS = 12          # тон, 30° на бин; +1 бин «серый» (насыщенность ~0)


def tone_hist(img):
    """Гистограмма тона (H,W,3) uint8 -> (TONE_BINS+1,) float, сумма = 1.

    Бин TONE_BINS — «серый» (max-min < 12): ахроматические пиксели не имеют
    тона и в тон-сравнении не участвуют, но терять их нельзя (чёрный фон,
    белый текст — иначе гистограмма врёт о доле цвета).
    """
    f = img.reshape(-1, 3).astype(np.int16)
    mx = f.max(axis=1).astype(np.float32)
    mn = f.min(axis=1).astype(np.float32)
    d = mx - mn
    grey = d < 12.0
    dd = np.where(d == 0, 1.0, d)
    # стандартная формула hue в градусах (0..360)
    r, g, b = f[:, 0].astype(np.float32), f[:, 1].astype(np.float32), f[:, 2].astype(np.float32)
    h = np.where(mx == r, 60.0 * (((g - b) / dd) % 6.0),
        np.where(mx == g, 60.0 * ((b - r) / dd + 2.0), 60.0 * ((r - g) / dd + 4.0)))
    h = np.where(h < 0, h + 360.0, h)
    binid = np.minimum((h / (360.0 / TONE_BINS)).astype(np.int32), TONE_BINS - 1)
    binid = np.where(grey, TONE_BINS, binid)
    hist = np.bincount(binid, minlength=TONE_BINS + 1).astype(np.float32)
    return hist / max(1.0, hist.sum())


def tone_metrics(img, tag):
    """Печатает тон-метрики кадра и возвращает (гистограмма, среднее B-R, средняя насыщенность)."""
    f = img.reshape(-1, 3).astype(np.int16)
    mx = f.max(axis=1).astype(np.float32)
    mn = f.min(axis=1).astype(np.float32)
    sat = np.where(mx > 0, (mx - mn) / np.where(mx == 0, 1.0, mx), 0.0)
    br = float((f[:, 2] - f[:, 0]).mean())        # B-R: пурпур/маджента > 0
    hist = tone_hist(img)
    top = np.argsort(-hist)[:3]
    print('%s: B-R %+.1f  насыщенность %.3f  серых %.1f%%  тон: %s'
          % (tag, br, float(sat.mean()), 100.0 * hist[TONE_BINS],
             ' '.join('%s=%.1f%%' % (('%d°' % (i * 30)) if i < TONE_BINS else 'grey',
                                     100.0 * hist[i]) for i in top)))
    return hist, br, float(sat.mean())


def tone_compare(a, b, tag_a, tag_b):
    ha, bra, sa = tone_metrics(a, tag_a)
    hb, brb, sb = tone_metrics(b, tag_b)
    l1 = float(np.abs(ha - hb).sum()) * 100.0
    print('тон-расхождение %s <-> %s: L1 %.1f  (B-R %+.1f vs %+.1f, насыщенность %.3f vs %.3f)'
          % (tag_a, tag_b, l1, bra, brb, sa, sb))
    return l1


# Полосы, в которых ищем линии: только мир. В HUD-полосах построчный профиль
# шумит из-за текста и иконок, там метрика бессмысленна.
LINE_BANDS = [(300, 200, 700, 450), (0, 300, 1021, 600), (60, 150, 960, 300)]


def line_defects(img, tag, thr=5.0, half=7, bands=None):
    """Тонкие горизонтальные линии в МИРЕ (дефект Vulkan-кадра 2026-10-21).

    Метрика: по каждой полосе считается профиль «жёлтости» строки —
    средней величины max(R,G) - B — и берётся отклонение строки от медиан
    соседей (строки y-half..y-1 и y+2..y+half). Строки-соседи слева и справа
    должны совпадать (иначе это край яркого блока, а не линия). Порог 5 по
    «жёлтости» отсекает обычный шум мира: замер 2026-10-21 на паре ab3 t50 —
    DXVK mean|dev| 0,29 и максимум 10,3 (одна строка), Vulkan 0,45 и шесть
    строк |dev|>5 (25,3 / 20,9 / 10,4 / 9,4 / 7,9 / 7,1).

    Прежние попытки (2026-10-21) не сработали: глобальный профиль светимости
    по всему кадру (7 групп против 4) и глобальный счётчик жёлтых пикселей
    (Vulkan 7419 против DXVK 10146 — даже в пользу DXVK) линии не видят,
    потому что они тонкие и полупрозрачные.
    """
    f = img.astype(np.float32)
    r, g, b = f[:, :, 0], f[:, :, 1], f[:, :, 2]
    yel = np.maximum(r, g) - b
    h, w = yel.shape
    hits, worst = [], 0.0
    for (x0, y0, x1, y1) in (bands or LINE_BANDS):
        x1, y1 = min(w, x1), min(h, y1)
        row = yel[y0:y1, x0:x1].mean(axis=1)
        n = len(row)
        dev = np.zeros(n, dtype=np.float32)
        for y in range(n):
            la, lb = max(0, y - half), max(0, y - 1)
            ra, rb = min(n, y + 2), min(n, y + half + 1)
            L = np.median(row[la:lb]) if lb > la else row[y]
            R = np.median(row[ra:rb]) if rb > ra else row[y]
            if abs(L - R) > 2.0:
                continue
            dev[y] = row[y] - 0.5 * (L + R)
        for y in range(n):
            if abs(dev[y]) > thr:
                hits.append(((x0, y0, x1, y1), y0 + y, float(dev[y])))
        worst = max(worst, float(np.abs(dev).max()))
    hits.sort(key=lambda t: -abs(t[2]))
    print('%s: строк с отклонением жёлтости >%.1f: %d  (макс |dev| %.1f)'
          % (tag, thr, len(hits), worst))
    for (bd, y, d) in hits[:6]:
        print('    полоса %s строка %d сдвиг %+.1f' % (bd, y, d))
    return len(hits)


def largest_rect(mask):
    """Площадь крупнейшего сплошного прямоугольника в булевой маске (H,W)
    и его координаты. Классика «largest rectangle in histogram»."""
    h, w = mask.shape
    heights = np.zeros(w, dtype=np.int32)
    best = (0, 0, 0, 0, 0)          # площадь, x0, y0, x1, y1
    for y in range(h):
        row = mask[y]
        heights = np.where(row, heights + 1, 0)
        stack = []                  # (столбец, высота)
        for x in range(w + 1):
            hh = int(heights[x]) if x < w else 0
            start = x
            while stack and stack[-1][1] >= hh:
                sx, sh = stack.pop()
                area = sh * (x - sx)
                if area > best[0]:
                    best = (area, sx, y - sh + 1, x, y + 1)
                start = sx
            stack.append((start, hh))
    return best


def defects(img, tag):
    h, w, _ = img.shape
    flat = img.reshape(-1, 3).astype(np.int16)
    white = np.all(flat >= 250, axis=1).reshape(h, w)
    mag = ((flat[:, 0] >= 230) & (flat[:, 1] <= 30) & (flat[:, 2] >= 230)).reshape(h, w)
    blk = np.all(flat == 0, axis=1).reshape(h, w)
    print('%s: %dx%d  белых %d (%.2f%%)  пурпурных %d (%.2f%%)  чёрных %d (%.2f%%)'
          % (tag, w, h, int(white.sum()), 100.0 * white.mean(),
             int(mag.sum()), 100.0 * mag.mean(), int(blk.sum()), 100.0 * blk.mean()))
    for name, m in (('белый', white), ('пурпурный', mag)):
        area, x0, y0, x1, y1 = largest_rect(m)
        if area:
            print('    крупнейший прямоугольник %s: %dx%d = %d px в (%d,%d)-(%d,%d)'
                  % (name, x1 - x0, y1 - y0, area, x0, y0, x1, y1))
    return white, mag


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == '--defects':
        for p in sys.argv[2:]:
            defects(load(p), p)
        return
    if sys.argv[1] == '--lines':
        for p in sys.argv[2:]:
            line_defects(load(p), p)
        return
    if sys.argv[1] == '--tone':
        ps = sys.argv[2:]
        if len(ps) == 2:
            tone_compare(load(ps[0]), load(ps[1]), ps[0], ps[1])
        else:
            for p in ps:
                tone_metrics(load(p), p)
        return
    img = load(sys.argv[1])
    lum = stats(img, 'КАДР ' + sys.argv[1])
    if len(sys.argv) > 2:
        ref = load(sys.argv[2])
        if ref.shape != img.shape:
            print('эталон %dx%d, кадр %dx%d — сравнение по центру пересечения'
                  % (ref.shape[1], ref.shape[0], img.shape[1], img.shape[0]))
            h = min(ref.shape[0], img.shape[0]); w = min(ref.shape[1], img.shape[1])
            ref, img = ref[:h, :w], img[:h, :w]
        rlum = stats(ref, 'ЭТАЛОН ' + sys.argv[2])
        d = np.abs(img.astype(np.int16) - ref.astype(np.int16)).max(axis=2)
        print('расхождение: средняя |Δ| %.1f, пикселей с |Δ|>32: %d (%.2f%%)'
              % (d.mean(), int((d > 32).sum()), 100.0 * (d > 32).mean()))
        print('светимость: кадр %.1f, эталон %.1f' % (lum.mean(), rlum.mean()))


if __name__ == '__main__':
    main()
