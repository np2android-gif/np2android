# -*- coding: utf-8 -*-
"""
ANDROID-FONT (Q15): font.tmp (漢字 ROM の代わり) に使う 16 ドットの字を、東雲フォントと
JFドットJISKan16s から 1 つのファイル (app/src/main/assets/font16.bin) にまとめる。

使い方 (README.md を参照):
    python mkfont16.py <shinonome-0.9.11 を展開したフォルダ> <JF-Dot-jiskan16s.ttf> <出力ファイル>

  - 全角: 東雲の shnmk16.bdf (JIS X 0208) を JIS の区点のまま置く。東雲に無い所 (13 区の NEC 特殊文字、
    89〜92 区の NEC 選定 IBM 拡張文字など) は、Shift JIS を MS932 (cp932) で Unicode にして、
    JF-Dot-jiskan16s.ttf に入っている 16 ドットのビットマップ (EBDT、16ppem) をそのまま使う。
    TTF の輪郭は描かない。
  - 半角: 東雲の shnm8x16r.bdf (JIS X 0201。5C は ¥) を使う。7C と 7E は、FONT.ROM に合わせて
    shnm8x16a.bdf (ISO8859-1) の ¦ (A6) と ~ (7E) にする。
  - どちらのフォントにも無い字は、アプリが端末のフォントで描く (FontGlyphs.kt)。

出力の形 (FontGlyphs.kt が読む。どれも 1 の画素が字):
    0      8 バイト   "NP2F16" 0x00 0x01
    8      4096       半角 256 字 x 16 行 (1 行 1 バイト、左の画素が上位ビット)
    4104   282752     全角 94 区 x 94 点 x 16 行 (1 行 2 バイト、ビッグエンディアン)
    286856 32         半角の、字がある印 (256 ビット。字 i は byte[i >> 3] の 0x80 >> (i & 7))
    286888 1105       全角の、字がある印 (94 x 94 ビット。区点 (ku, ten は 0 から) は i = ku * 94 + ten)
  合わせて 287,993 バイト。印の無い所は、アプリが端末のフォントで描く。
  「どのフォントの字か」を区ごとに数えて表示する (報告のため)。
"""
import collections
import sys

from fontTools.ttLib import TTFont

MAGIC = b'NP2F16\x00\x01'
ANK_SIZE = 256 * 16
KNJ_SIZE = 94 * 94 * 32


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


class JfBitmaps:
    """TTF の EBDT (16ppem) のビットマップを、16 行の値 (左の画素が 0x8000) で取り出す"""

    def __init__(self, path):
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


def main():
    shn_dir, jf_path, out_path = sys.argv[1:4]
    k16 = read_bdf(shn_dir + '/bdf/shnmk16.bdf')
    a16r = read_bdf(shn_dir + '/bdf/shnm8x16r.bdf')
    a16a = read_bdf(shn_dir + '/bdf/shnm8x16a.bdf')
    jf = JfBitmaps(jf_path)

    ank = bytearray(ANK_SIZE)
    knj = bytearray(KNJ_SIZE)
    ank_mark = bytearray(32)
    knj_mark = bytearray((94 * 94 + 7) // 8)

    # ---- 半角
    for code in list(range(0x20, 0x7f)) + list(range(0xa1, 0xe0)):
        if code == 0x7c:
            rows = a16a[0xa6]          # ¦
        elif code == 0x7e:
            rows = a16a[0x7e]          # ~
        else:
            rows = a16r[code]          # 5C は ¥
        ank[code * 16:code * 16 + 16] = bytes(rows)
        ank_mark[code >> 3] |= 0x80 >> (code & 7)

    # ---- 全角
    count = collections.OrderedDict()
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
            o = (ku * 94 + ten) * 32
            for y in range(16):
                knj[o + y * 2] = rows[y] >> 8
                knj[o + y * 2 + 1] = rows[y] & 0xff
            i = ku * 94 + ten
            knj_mark[i >> 3] |= 0x80 >> (i & 7)
            c = count.setdefault(ku + 1, collections.Counter())
            c[src] += 1

    out = MAGIC + ank + knj + ank_mark + knj_mark
    with open(out_path, 'wb') as f:
        f.write(out)
    for ku, c in count.items():
        print('ku %2d: shinonome %2d, jiskan16s %2d' % (ku, c['shinonome'], c['jiskan16s']))
    print('total: shinonome %d, jiskan16s %d' % (
        sum(c['shinonome'] for c in count.values()), sum(c['jiskan16s'] for c in count.values())))
    print('%s: %d bytes' % (out_path, len(out)))


if __name__ == '__main__':
    main()
