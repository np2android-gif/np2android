# -*- coding: utf-8 -*-
"""
ANDROID-FONT (Q15): font.tmp (漢字 ROM の代わり) に使う 16 ドットの字を、東雲フォントと
JFドットJISKan16s から 1 つのファイル (app/src/main/assets/font16.bin) にまとめる。
Q15b: 字ごとに差し替えられるようにした (override/ の PNG と CSV。README.md を参照)。

使い方 (README.md を参照):
    python mkfont16.py build <shinonome-0.9.11 を展開したフォルダ> <JF-Dot-jiskan16s.ttf> <出力ファイル>
            [--override <フォルダ> | --no-override]
    python mkfont16.py export <font16.bin> <出力の名前> <字> [<字> ...] [--cols N]

  - 全角: 東雲の shnmk16.bdf (JIS X 0208) を JIS の区点のまま置く。東雲に無い所 (13 区の NEC 特殊文字、
    89〜92 区の NEC 選定 IBM 拡張文字など) は、Shift JIS を MS932 (cp932) で Unicode にして、
    JF-Dot-jiskan16s.ttf に入っている 16 ドットのビットマップ (EBDT、16ppem) をそのまま使う。
    TTF の輪郭は描かない。
  - 半角: 東雲の shnm8x16r.bdf (JIS X 0201。5C は ¥) を使う。7C と 7E は、FONT.ROM に合わせて
    shnm8x16a.bdf (ISO8859-1) の ¦ (A6) と ~ (7E) にする。
  - 差し替え: override/ (--override で変えられる) にある「編集用の PNG + 一覧の CSV」の字を、上の 2 つより
    優先する。差し替えた字には「ある字」の印が付くので、アプリが端末のフォントで描くことはない。
  - どれにも無い字は、アプリが端末のフォントで描く (FontGlyphs.kt)。

出力の形 (FontGlyphs.kt が読む。どれも 1 の画素が字):
    0      8 バイト   "NP2F16" 0x00 0x01
    8      4096       半角 256 字 x 16 行 (1 行 1 バイト、左の画素が上位ビット)
    4104   282752     全角 94 区 x 94 点 x 16 行 (1 行 2 バイト、ビッグエンディアン)
    286856 32         半角の、字がある印 (256 ビット。字 i は byte[i >> 3] の 0x80 >> (i & 7))
    286888 1105       全角の、字がある印 (94 x 94 ビット。区点 (ku, ten は 0 から) は i = ku * 94 + ten)
  合わせて 287,993 バイト。印の無い所は、アプリが端末のフォントで描く。
  「どのフォントの字か」を区ごとに数えて表示する (報告のため)。

字の指定 (export の <字>、CSV の「区点」の列):
    89-15     全角の区点 (1 から)
    H7C       半角の文字コード (16 進 2 桁)
    U+4EFC    Unicode (MS932 で区点か半角にする)
    @一覧.txt  ファイルの中の、空白・改行で区切った指定 (# から行末までは注釈)
    そのほか  書いた文字を 1 字ずつ (MS932 で区点か半角にする)
"""
import argparse
import collections
import csv
import os
import re
import sys

MAGIC = b'NP2F16\x00\x01'
ANK_SIZE = 256 * 16
KNJ_SIZE = 94 * 94 * 32
OFF_ANK = len(MAGIC)
OFF_KNJ = OFF_ANK + ANK_SIZE
OFF_ANK_MARK = OFF_KNJ + KNJ_SIZE
OFF_KNJ_MARK = OFF_ANK_MARK + 32
TOTAL = OFF_KNJ_MARK + (94 * 94 + 7) // 8

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- 編集用の PNG の形
CELL = 16                   # 1 字のマス (半角は左の 8 列だけを使い、右の 8 列は境の線と同じ灰色)
PITCH = CELL + 1            # マスの間に 1 画素の境の線
GRID = (128, 128, 128)      # 境の線の色
INK = (0, 0, 0)
PAPER = (255, 255, 255)
CSV_HEADER = ['マス', '区点', '文字', 'Unicode', '作り方']
# 空白のままでよい字 (ほかの字のマスが空白なら、描き忘れとして止める)
BLANK_OK = {('H', 0x20), ('K', 0, 0)}


# ======================================================================
# 元のフォントを読む

def read_bdf(path):
    """BDF の字を {ENCODING: 行の値のリスト} にする。どの字も 1 文字の枠いっぱい (BBX が FONTBOUNDINGBOX と同じ) であること"""
    glyphs = {}
    fbb = None
    enc = bbx = bits = None
    for line in open(path, encoding='latin-1'):
        w = line.split()
        if not w:
            continue
        if w[0] == 'FONTBOUNDINGBOX':
            fbb = list(map(int, w[1:5]))
        elif w[0] == 'ENCODING':
            enc = int(w[1])
        elif w[0] == 'BBX':
            bbx = list(map(int, w[1:5]))
        elif w[0] == 'BITMAP':
            bits = []
        elif w[0] == 'ENDCHAR':
            assert bbx == fbb, (path, enc, bbx, fbb)
            assert len(bits) == fbb[1], (path, enc)
            glyphs[enc] = bits
            bits = None
        elif bits is not None:
            bits.append(int(w[0], 16))
    assert fbb is not None and fbb[1] == 16 and fbb[3] == -2, (path, fbb)
    return glyphs


def jis_to_sjis(ku, ten):
    """区・点 (0 から) を Shift JIS の 2 バイトにする"""
    j1, j2 = ku + 0x21, ten + 0x21
    s1 = ((j1 + 1) >> 1) + (0x70 if j1 <= 0x5e else 0xb0)
    if j1 & 1:
        s2 = j2 + (0x20 if j2 >= 0x60 else 0x1f)
    else:
        s2 = j2 + 0x7e
    return bytes([s1, s2])


def sjis_to_jis(b):
    """Shift JIS の 2 バイトを区・点 (0 から) にする"""
    s1, s2 = b
    ku = (s1 - (0x70 if s1 <= 0x9f else 0xb0)) * 2 - 1
    if s2 >= 0x9f:
        ku += 1
        ten = s2 - 0x7e
    else:
        ten = s2 - (0x20 if s2 >= 0x80 else 0x1f)
    return ku - 0x21, ten - 0x21


class JfBitmaps:
    """TTF の EBDT (16ppem) のビットマップを、16 行の値 (左の画素が 0x8000) で取り出す"""

    def __init__(self, path):
        from fontTools.ttLib import TTFont
        self.tt = TTFont(path)
        self.cmap = self.tt.getBestCmap()
        strikes = [s for s in self.tt['EBLC'].strikes if s.bitmapSizeTable.ppemY == 16]
        assert len(strikes) == 1, 'no 16ppem strike'
        idx = self.tt['EBLC'].strikes.index(strikes[0])
        self.data = self.tt['EBDT'].strikeData[idx]
        self.metrics = {}
        for ist in strikes[0].indexSubTables:
            m = getattr(ist, 'metrics', None)
            for name in ist.names:
                self.metrics[name] = m
        # 行の位置: 上端の行 = ascender 14 (東雲と同じ。BDF の FONTBOUNDINGBOX 16 16 0 -2)
        self.ascent = 14

    def get(self, ch):
        gid = self.cmap.get(ord(ch))
        if gid is None or gid not in self.data:
            return None
        g = self.data[gid]
        m = getattr(g, 'metrics', None) or self.metrics.get(gid)
        assert m is not None, gid
        rows = [0] * 16
        top = self.ascent - m.horiBearingY
        for y in range(m.height):
            r = g.getRow(y, bitDepth=1, metrics=m)
            v = int.from_bytes(r, 'big')
            w = len(r) * 8
            for x in range(m.width):
                if (v >> (w - 1 - x)) & 1:
                    cy, cx = top + y, m.horiBearingX + x
                    assert 0 <= cy < 16 and 0 <= cx < 16, (ch, cy, cx)
                    rows[cy] |= 0x8000 >> cx
        return rows


# ======================================================================
# 字の指定と font16.bin

class FontError(Exception):
    pass


def key_label(key):
    """('K', ku, ten) → '89-15'、('H', code) → 'H7C'"""
    if key[0] == 'K':
        return '%d-%02d' % (key[1] + 1, key[2] + 1)
    return 'H%02X' % key[1]


def key_char(key):
    """字の Unicode の文字 (無ければ '')"""
    try:
        if key[0] == 'K':
            return jis_to_sjis(key[1], key[2]).decode('cp932')
        if key[1] == 0x5c:
            return '¥'
        if key[1] == 0x7e:
            return '~'
        if key[1] == 0x7c:
            return '¦'
        return bytes([key[1]]).decode('cp932')
    except UnicodeDecodeError:
        return ''


def char_key(ch):
    try:
        b = ch.encode('cp932')
    except UnicodeEncodeError:
        raise FontError('MS932 に無い文字です: %r (U+%04X)' % (ch, ord(ch)))
    if len(b) == 1:
        return ('H', b[0])
    return ('K',) + sjis_to_jis(b)


def parse_key(tok):
    """1 つの指定を key にする (区点・H・U+ の形だけ。文字の並びは parse_keys が分ける)"""
    m = re.fullmatch(r'(\d{1,2})-(\d{1,2})', tok)
    if m:
        ku, ten = int(m.group(1)), int(m.group(2))
        if not (1 <= ku <= 94 and 1 <= ten <= 94):
            raise FontError('区点が範囲の外です: %s' % tok)
        return ('K', ku - 1, ten - 1)
    m = re.fullmatch(r'[Hh]([0-9A-Fa-f]{2})', tok)
    if m:
        return ('H', int(m.group(1), 16))
    m = re.fullmatch(r'[Uu]\+([0-9A-Fa-f]{4,6})', tok)
    if m:
        return char_key(chr(int(m.group(1), 16)))
    return None


def parse_keys(tokens):
    keys = []
    for tok in tokens:
        if tok.startswith('@'):
            with open(tok[1:], encoding='utf-8-sig') as f:
                text = re.sub(r'#.*', '', f.read())
            keys += parse_keys(text.split())
            continue
        k = parse_key(tok)
        if k is not None:
            keys.append(k)
        else:
            keys += [char_key(ch) for ch in tok]
    return keys


class Font16:
    """font16.bin の中身。字は 16 行の値 (全角は左の画素が 0x8000、半角は 0x80)"""

    def __init__(self, data=None):
        if data is None:
            data = MAGIC + bytes(TOTAL - len(MAGIC))
        if len(data) != TOTAL or data[:len(MAGIC)] != MAGIC:
            raise FontError('font16.bin の形ではありません')
        self.b = bytearray(data)

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as f:
            return cls(f.read())

    def has(self, key):
        if key[0] == 'K':
            i = key[1] * 94 + key[2]
            return bool(self.b[OFF_KNJ_MARK + (i >> 3)] & (0x80 >> (i & 7)))
        c = key[1]
        return bool(self.b[OFF_ANK_MARK + (c >> 3)] & (0x80 >> (c & 7)))

    def get(self, key):
        if key[0] == 'K':
            o = OFF_KNJ + (key[1] * 94 + key[2]) * 32
            return [(self.b[o + y * 2] << 8) | self.b[o + y * 2 + 1] for y in range(16)]
        o = OFF_ANK + key[1] * 16
        return list(self.b[o:o + 16])

    def put(self, key, rows):
        if key[0] == 'K':
            i = key[1] * 94 + key[2]
            o = OFF_KNJ + i * 32
            for y in range(16):
                self.b[o + y * 2] = rows[y] >> 8
                self.b[o + y * 2 + 1] = rows[y] & 0xff
            self.b[OFF_KNJ_MARK + (i >> 3)] |= 0x80 >> (i & 7)
        else:
            c = key[1]
            self.b[OFF_ANK + c * 16:OFF_ANK + c * 16 + 16] = bytes(rows)
            self.b[OFF_ANK_MARK + (c >> 3)] |= 0x80 >> (c & 7)


def width(key):
    return CELL if key[0] == 'K' else 8


def bit(key, rows, x, y):
    return (rows[y] >> (width(key) - 1 - x)) & 1


# ======================================================================
# 編集用の PNG・一覧の CSV

def sheet_size(n, cols):
    rows = max(1, (n + cols - 1) // cols)
    return 1 + min(n, cols) * PITCH, 1 + rows * PITCH


def write_sheet(prefix, entries, cols=16):
    """entries: [(key, rows or None, 作り方)]。<prefix>.png と <prefix>.csv を書く"""
    from PIL import Image
    w, h = sheet_size(len(entries), cols)
    img = Image.new('RGB', (w, h), GRID)
    px = img.load()
    for n, (key, rows, _) in enumerate(entries):
        x0, y0 = 1 + (n % cols) * PITCH, 1 + (n // cols) * PITCH
        for y in range(CELL):
            for x in range(width(key)):
                px[x0 + x, y0 + y] = INK if rows is not None and bit(key, rows, x, y) else PAPER
    img.save(prefix + '.png')
    with open(prefix + '.csv', 'w', encoding='utf-8-sig', newline='') as f:
        wr = csv.writer(f)
        wr.writerow(CSV_HEADER)
        for n, (key, rows, how) in enumerate(entries):
            ch = key_char(key)
            wr.writerow([n + 1, key_label(key), ch, 'U+%04X' % ord(ch) if ch else '', how])


def read_sheet(prefix):
    """<prefix>.png と <prefix>.csv を読んで [(key, rows, 作り方)] にする。おかしな所があれば FontError"""
    from PIL import Image
    name = os.path.basename(prefix)
    with open(prefix + '.csv', encoding='utf-8-sig', newline='') as f:
        rd = csv.reader(f)
        head = next(rd, None)
        if head is None or [c.strip() for c in head[:2]] != CSV_HEADER[:2]:
            raise FontError('%s.csv: 1 行目が「%s」ではありません' % (name, ','.join(CSV_HEADER)))
        lines = [r for r in rd if any(c.strip() for c in r)]
    img = Image.open(prefix + '.png')
    if img.mode in ('RGBA', 'LA', 'PA') or (img.mode == 'P' and 'transparency' in img.info):
        alpha = img.convert('RGBA').getchannel('A')
        if alpha.getextrema()[0] < 255:
            raise FontError('%s.png: 透明な画素があります (不透明で保存してください)' % name)
    img = img.convert('RGB')
    px = img.load()
    errors = []
    cells = {}
    for r in lines:
        try:
            pos = int(r[0])
            key = parse_key(r[1].strip())
            if key is None:
                raise ValueError
        except (ValueError, IndexError, FontError):
            raise FontError('%s.csv: 読めない行です: %s' % (name, ','.join(r)))
        if pos in cells:
            raise FontError('%s.csv: マス %d が 2 回あります' % (name, pos))
        cells[pos] = (key, r[4].strip() if len(r) > 4 else '')
    if not cells:
        return []
    last = max(cells)
    cols = (img.size[0] - 1) // PITCH
    if cols < 1 or img.size[0] != 1 + cols * PITCH or (img.size[1] - 1) % PITCH:
        raise FontError('%s.png: 大きさ %dx%d が、マス目 (1 字 %d 画素 + 境の線 1 画素) に合いません' % (
            name, img.size[0], img.size[1], CELL, ))
    if (last - 1) // cols >= (img.size[1] - 1) // PITCH:
        raise FontError('%s.png: マス %d が画像の外です' % (name, last))
    # 境の線: マスの外側はすべて灰色
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            if (x % PITCH == 0 or y % PITCH == 0) and px[x, y] != GRID:
                errors.append('境の線 (%d, %d) が灰色 %s ではなく %s' % (x, y, GRID, px[x, y]))
                if len(errors) > 20:
                    break
        if len(errors) > 20:
            break
    out = []
    for pos in sorted(cells):
        key, how = cells[pos]
        n = pos - 1
        x0, y0 = 1 + (n % cols) * PITCH, 1 + (n // cols) * PITCH
        rows = [0] * 16
        for y in range(CELL):
            for x in range(CELL):
                c = px[x0 + x, y0 + y]
                if x >= width(key):
                    if c != GRID:
                        errors.append('マス %d (%s %s): 半角のマスの右半分 (%d, %d) は灰色のままにしてください' % (
                            pos, key_label(key), key_char(key), x, y))
                elif c == INK:
                    rows[y] |= 1 << (width(key) - 1 - x)
                elif c != PAPER:
                    errors.append('マス %d (%s %s): 点 (%d, %d) が黒でも白でもありません %s' % (
                        pos, key_label(key), key_char(key), x, y, c))
        if not any(rows) and key not in BLANK_OK:
            errors.append('マス %d (%s %s): 字がありません (空白のまま)' % (pos, key_label(key), key_char(key)))
        out.append((key, rows, how))
    if errors:
        more = '' if len(errors) <= 20 else '\n  ほか %d 件' % (len(errors) - 20)
        raise FontError('%s.png:\n  %s%s' % (name, '\n  '.join(errors[:20]), more))
    return out


def read_overrides(folder):
    """フォルダの中の「同じ名前の .csv と .png」をすべて読む。{key: (rows, 作り方, ファイル名)}"""
    result = {}
    if not os.path.isdir(folder):
        return result
    for fn in sorted(os.listdir(folder)):
        if not fn.lower().endswith('.csv'):
            continue
        prefix = os.path.join(folder, fn[:-4])
        if not os.path.exists(prefix + '.png'):
            raise FontError('%s: 同じ名前の .png がありません' % fn)
        for key, rows, how in read_sheet(prefix):
            if key in result:
                raise FontError('%s %s が %s と %s の 2 つにあります' % (
                    key_label(key), key_char(key), result[key][2], fn))
            result[key] = (rows, how, fn)
    return result


# ======================================================================
# 見るための拡大 PNG (ラベルの文字も font16.bin の字で書く)

def draw_text(px, font, text, x, y, scale, color):
    for ch in text:
        try:
            key = char_key(ch)
        except FontError:
            key = None
        w = 8 if key is None or key[0] == 'H' else 16
        if key is not None and font.has(key):
            rows = font.get(key)
            for yy in range(16):
                for xx in range(w):
                    if bit(key, rows, xx, yy):
                        for dy in range(scale):
                            for dx in range(scale):
                                px[x + xx * scale + dx, y + yy * scale + dy] = color
        x += w * scale


def write_view(path, font, entries, columns, scale=4, per_row=8):
    """拡大 PNG。entries: [key]、columns: [(見出し, {key: rows})]。字ごとに columns を横に並べ、下に文字と区点を書く"""
    from PIL import Image
    gw = CELL * scale
    cw = len(columns) * (gw + 4) + 12          # 1 字の欄の幅
    ch = 16 * 2 + 4 + gw + 4 + 16 * 2 + 12     # 見出し + 字 + 文字と区点
    n = len(entries)
    cols = min(n, per_row)
    rows_n = (n + per_row - 1) // per_row
    img = Image.new('RGB', (max(1, cols) * cw + 8, max(1, rows_n) * ch + 8), (255, 255, 255))
    px = img.load()
    for i, key in enumerate(entries):
        x0 = 8 + (i % per_row) * cw
        y0 = 4 + (i // per_row) * ch
        for c, (title, glyphs) in enumerate(columns):
            x = x0 + c * (gw + 4)
            draw_text(px, font, title, x, y0, 1, (90, 90, 90))
            y = y0 + 16 + 4
            for yy in range(y - 1, y + gw + 1):
                for xx in (x - 1, x + gw):
                    px[xx, yy] = (200, 200, 200)
            for xx in range(x - 1, x + gw + 1):
                px[xx, y - 1] = px[xx, y + gw] = (200, 200, 200)
            rows = glyphs.get(key)
            if rows is None:
                continue
            for yy in range(16):
                for xx in range(width(key)):
                    if bit(key, rows, xx, yy):
                        for dy in range(scale):
                            for dx in range(scale):
                                px[x + xx * scale + dx, y + yy * scale + dy] = (0, 0, 0)
        ty = y0 + 16 + 4 + gw + 6
        draw_text(px, font, key_char(key) + ' ' + key_label(key), x0, ty, 1, (0, 0, 0))
    img.save(path)


# ======================================================================
# 命令

def build(args):
    k16 = read_bdf(os.path.join(args.shinonome, 'bdf', 'shnmk16.bdf'))
    a16r = read_bdf(os.path.join(args.shinonome, 'bdf', 'shnm8x16r.bdf'))
    a16a = read_bdf(os.path.join(args.shinonome, 'bdf', 'shnm8x16a.bdf'))
    jf = JfBitmaps(args.jiskan16s)
    font = Font16()

    # ---- 半角
    for code in list(range(0x20, 0x7f)) + list(range(0xa1, 0xe0)):
        if code == 0x7c:
            rows = a16a[0xa6]          # ¦
        elif code == 0x7e:
            rows = a16a[0x7e]          # ~
        else:
            rows = a16r[code]          # 5C は ¥
        font.put(('H', code), rows)

    # ---- 全角
    count = collections.OrderedDict()
    srcs = {}
    for ku in range(94):
        for ten in range(94):
            jis = ((ku + 0x21) << 8) | (ten + 0x21)
            rows = k16.get(jis)
            src = 'shinonome'
            if rows is None:
                try:
                    ch = jis_to_sjis(ku, ten).decode('cp932')
                except UnicodeDecodeError:
                    continue
                rows = jf.get(ch)
                src = 'jiskan16s'
                if rows is None:
                    continue
            font.put(('K', ku, ten), rows)
            srcs[('K', ku, ten)] = src
            count.setdefault(ku + 1, collections.Counter())[src] += 1

    # ---- 差し替え
    folder = None if args.no_override else (args.override or os.path.join(HERE, 'override'))
    ov = read_overrides(folder) if folder else {}
    n_ank = 0
    for key, (rows, how, fn) in sorted(ov.items()):
        if key[0] == 'K':
            c = count.setdefault(key[1] + 1, collections.Counter())
            if key in srcs:
                c[srcs[key]] -= 1
            c['override'] += 1
        else:
            n_ank += 1
        font.put(key, rows)

    with open(args.out, 'wb') as f:
        f.write(font.b)
    for ku, c in sorted(count.items()):
        print('ku %2d: shinonome %2d, jiskan16s %2d, override %2d' % (
            ku, c['shinonome'], c['jiskan16s'], c['override']))
    tot = collections.Counter()
    for c in count.values():
        tot.update(c)
    print('total: shinonome %d, jiskan16s %d, override %d (+ half-width %d)' % (
        tot['shinonome'], tot['jiskan16s'], tot['override'], n_ank))
    if folder:
        print('override: %s (%d 字)' % (folder, len(ov)))
    print('%s: %d bytes' % (args.out, len(font.b)))


def export(args):
    font = Font16.load(args.font16)
    keys = parse_keys(args.chars)
    entries = []
    missing = []
    for key in keys:
        if font.has(key):
            entries.append((key, font.get(key), 'font16.bin'))
        else:
            entries.append((key, None, '(無し: 描いてください)'))
            missing.append(key_label(key) + ' ' + key_char(key))
    write_sheet(args.prefix, entries, args.cols)
    write_view(args.prefix + '_view.png', font, keys, [('今の字', {k: r for k, r, _ in entries if r})])
    print('%s.png / .csv / _view.png: %d 字' % (args.prefix, len(entries)))
    if missing:
        print('font16.bin に無い字 (空白のマスにしました。描かないと読み込めません): ' + ', '.join(missing))


def main():
    ap = argparse.ArgumentParser(description='font16.bin を作る・字のシートを書き出す')
    sub = ap.add_subparsers(dest='cmd', required=True)
    b = sub.add_parser('build', help='font16.bin を作る')
    b.add_argument('shinonome', help='shinonome-0.9.11 を展開したフォルダ')
    b.add_argument('jiskan16s', help='JF-Dot-jiskan16s.ttf')
    b.add_argument('out', help='出力する font16.bin')
    g = b.add_mutually_exclusive_group()
    g.add_argument('--override', help='差し替えの PNG・CSV のフォルダ (既定はこのスクリプトの隣の override/)')
    g.add_argument('--no-override', action='store_true', help='差し替えを使わない')
    e = sub.add_parser('export', help='字を編集用の PNG・CSV と拡大 PNG に書き出す')
    e.add_argument('font16', help='font16.bin')
    e.add_argument('prefix', help='出力の名前 (<名前>.png・<名前>.csv・<名前>_view.png)')
    e.add_argument('chars', nargs='+', help='字 (89-15、H7C、U+4EFC、@一覧.txt、文字そのもの)')
    e.add_argument('--cols', type=int, default=16, help='編集用の PNG の 1 行のマスの数 (既定 16)')
    args = ap.parse_args()
    try:
        build(args) if args.cmd == 'build' else export(args)
    except FontError as ex:
        sys.exit('エラー: %s' % ex)


if __name__ == '__main__':
    main()
