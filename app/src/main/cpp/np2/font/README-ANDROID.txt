font/ (漢字 ROM の代わりのフォント)
====================================

xnp2 の font/ をもとにしている。ここでは、font.tmp の字を東雲フォントなどから作るために Android 向けに
変えたところを書く (Q15)。変更箇所には /* ANDROID-FONT */ の印を付けている (port/androidfont.c、
port/np2bridge.c、Kotlin の FontGlyphs.kt・Np2Native.kt・Np2Activity.kt にも同じ印か説明がある)。

フォントを読む順番 (font.c の font_load。変えていない)
  作業フォルダの FONT.BMP → FONT.ROM → PC88.FNT の順に読み、まだ足りない字があれば font.tmp を読む。
  font.tmp が無ければ、makepc98bmp (fontmake.c) で作る。利用者が置いた FONT.BMP・FONT.ROM が優先される。

a22 までの font.tmp
  makepc98bmp は字の形を fontmng からもらう。Android 版の fontmng (sdl2/fontmng.c) は RESOURCE_US で
  英数字 (ank12) しか持たず、知らない字には「?」の形を返すので、font.tmp の漢字・かな・半角カナ・
  NEC 特殊文字は、すべて「?」になっていた。DOS の日本語は「????」と出た。

字の形を Kotlin から渡す (a23 から)
  - makepc98bmp が字の形をもらう先を、font.tmp を作るときだけ port/androidfont.c に切り替える
    (fontmake.c の getfont)。PC-98 に無い字の除外 (ispc98jis)、78/83 JIS の入れ替え、内蔵の字
    (fontdata_16・29〜2c) の上書きは makepc98bmp のまま。内蔵メニューの文字 (fontmng) は変えていない。
  - androidfont_create は JNI で Kotlin の Np2Native.renderFontGlyphs (FontGlyphs.render) を 1 回だけ
    呼び、全部の字 (半角 256 字と全角 94 区 x 94 点) のバイト列をもらう。1 字ずつは呼ばない。

字の形の出どころ (FontGlyphs.render)
  - assets の font16.bin。東雲フォント 0.9.11 (16 ドット) と JFドットJISKan16s の 16 ドットのビットマップを、
    PC で 1 つにまとめたもの (作り方とファイルの形は tools/font16/README.md と mkfont16.py)。
    - 全角: 東雲 (JIS X 0208) を区点のまま。東雲に無い 13 区 (NEC 特殊文字) と 89〜92 区 (NEC 選定 IBM
      拡張文字) は JISKan16s。拡大・縮小せず、16 ドットのまま置く。
    - 半角: 東雲の JIS X 0201 版 (5C は ¥)。7C と 7E は、FONT.ROM に合わせて東雲の ISO8859-1 版の ¦ と ~。
  - font16.bin に無い全角の字 (makepc98bmp が使うものでは、89〜92 区のまれな漢字と ∑・￤ の 86 字) だけ、
    端末のフォント (既定のフォント、textLocale = ja) で描く。Shift JIS を MS932 (windows-31j) として
    Unicode にする。アンチエイリアスなしで描き (しきい値で切るやり方は、16 ドットでは細い線が消えるか、
    太りすぎた)、上下は「国」の中心を 8 ドット目に合わせる (東雲の漢字と同じ高さ)。端末のフォントにも
    無い字は空白。どの区の字を端末のフォントで描いたかは logcat の np2font に出る。
  - IBM 拡張文字の異体字 (U+FA12 の「晴」など 11 字) は、普通の字形の東雲では置き換えず、端末のフォントで
    正しい字形を描く。
  - XZ3 で 60〜100 ミリ秒。描いている間の「フォントを準備しています」は、0.3 秒より短ければ出ない。
  - font16.bin が読めなかったときは、今までどおり fontmng で英数字だけの font.tmp を作る。

古い font.tmp の作り直し
  - Kotlin の Settings に「font.tmp を作った仕組みの版」(font_tmp_version、FontGlyphs.VERSION) と、
    字の形を渡した時刻 (font_tmp_made_at) を置く。字の形を渡せたときだけ記録する。
    版 1 は端末のフォントで描いたもの (a23 の前のテスト版だけ)、版 2 は東雲フォントなど。
  - 起動時 (エミュレータのスレッドが動き出す前) に、記録した版が古く、次のどれかなら font.tmp を消す。
    次の font_load で作り直される。
    - a22 までの「?」のもの (「亜」(JIS 3021) が全角の「？」(JIS 2129) と同じ形)
    - 版 1 の記録がある (版 1 は時刻を記録していない)
    - 版 2 から: font.tmp の更新時刻が、記録した時刻の 5 秒前から 60 秒後まで (このアプリが作ったもの)
    外から写した font.tmp (漢字の入ったもの) や、FONT.BMP・FONT.ROM は消さない。
  - ステートセーブ (statsave.c の gaiji) に入るのは外字の部分だけで、漢字 ROM の全体は入らない。
    レジュームやステートロードの後も、作り直した font.tmp の字が使われる。
