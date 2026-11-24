font/ (漢字 ROM の代わりのフォント)
====================================

xnp2 の font/ をもとにしている。ここでは、font.tmp を端末の日本語フォントから作るために Android 向けに
変えたところを書く (Q15)。変更箇所には /* ANDROID-FONT */ の印を付けている (port/androidfont.c、
port/np2bridge.c、Kotlin の FontGlyphs.kt・Np2Native.kt・Np2Activity.kt にも同じ印か説明がある)。

フォントを読む順番 (font.c の font_load。変えていない)
  作業フォルダの FONT.BMP → FONT.ROM → PC88.FNT の順に読み、まだ足りない字があれば font.tmp を読む。
  font.tmp が無ければ、makepc98bmp (fontmake.c) で作る。利用者が置いた FONT.BMP・FONT.ROM が優先される。

a22 までの font.tmp
  makepc98bmp は字の形を fontmng からもらう。Android 版の fontmng (sdl2/fontmng.c) は RESOURCE_US で
  英数字 (ank12) しか持たず、知らない字には「?」の形を返すので、font.tmp の漢字・かな・半角カナ・
  NEC 特殊文字は、すべて「?」になっていた。DOS の日本語は「????」と出た。

端末の日本語フォントから作る (a23 から)
  - makepc98bmp が字の形をもらう先を、font.tmp を作るときだけ port/androidfont.c に切り替える
    (fontmake.c の getfont)。PC-98 に無い字の除外 (ispc98jis)、78/83 JIS の入れ替え、内蔵の字
    (fontdata_16・29〜2c) の上書きは makepc98bmp のまま。内蔵メニューの文字 (fontmng) は変えていない。
  - androidfont_create は JNI で Kotlin の Np2Native.renderFontGlyphs (FontGlyphs.render) を 1 回だけ
    呼び、全部の字 (半角 256 字と全角 94 区 x 94 点) を描いたバイト列をもらう。1 字ずつは呼ばない。
  - 字には UTF-8 に変える前の Shift JIS をそのまま渡し、Kotlin で MS932 (windows-31j) として Unicode
    にする。NEC 特殊文字 (13 区) も読める。半角の 5C は ¥、7C は ¦、7E は ‾ (FONT.ROM と同じ)。
  - フォントは、Android 10 以上ではシステムが日本語に使うもの (SystemFonts で言語が ja のもののうち、
    既定のフォントで描いた「亜あ」と同じ形になるもの) を先頭に置き、無い字はシステムのフォントで補う。
    Android 9 以下は、既定のフォントを日本語の字体 (textLocale = ja) で使う。XZ3 (Android 10) では
    SomcUDGothic になった。使ったフォントの名前は logcat の np2font に出る。フォントにも無い字
    (豆腐になるもの) は空白にする。
  - 全角は 16x16、半角は 8x16。上下は「国」の中心を 8 ドット目に合わせる。字幅が 8 を超える半角は
    横を縮める。アンチエイリアスなしで描く (しきい値で切るやり方は、16 ドットでは細い線が消えるか、
    太りすぎた)。
  - 日本語フォントが無い端末や、描けなかったときは、今までどおり fontmng で英数字だけの font.tmp を作る。
  - 描くのに XZ3 で約 3 秒かかる (多くは SystemFonts.getAvailableFonts)。作るのは font.tmp が無いとき
    だけなので、その間だけ「フォントを準備しています」を出す (0.3 秒より短ければ出さない)。

古い font.tmp の作り直し
  - Kotlin の Settings に「font.tmp を作った仕組みの版」(font_tmp_version、FontGlyphs.VERSION) を置く。
    端末の日本語フォントで描けたときだけ記録する。
  - 起動時 (エミュレータのスレッドが動き出す前) に、記録した版が古く、かつ font.tmp が a22 までの「?」の
    もの (「亜」(JIS 3021) が全角の「？」(JIS 2129) と同じ形) なら消す。次の font_load で作り直される。
    外から写した font.tmp (漢字の入ったもの) や、FONT.BMP・FONT.ROM は消さない。日本語フォントが
    無い端末では、作り直しても同じなので消さない。
  - ステートセーブ (statsave.c の gaiji) に入るのは外字の部分だけで、漢字 ROM の全体は入らない。
    レジュームやステートロードの後も、作り直した font.tmp の字が使われる。
