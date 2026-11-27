# 漢字のフォントのデータ（font16.bin）

アプリに同梱する `app/src/main/assets/font16.bin` の作り方です。アプリは、作業フォルダに `font.tmp`（漢字 ROM の代わり）を作るときに、このファイルから字の形を取ります（`FontGlyphs.kt`、[np2/font/README-ANDROID.txt](../../app/src/main/cpp/np2/font/README-ANDROID.txt)）。

## もとにしたフォント

| フォント | 使うファイル | 入手した場所 | SHA-256 | ライセンス |
| --- | --- | --- | --- | --- |
| 東雲フォント 0.9.11 | `shinonome-0.9.11p1.tar.bz2` の `bdf/shnmk16.bdf`・`shnm8x16r.bdf`・`shnm8x16a.bdf` | http://openlab.ring.gr.jp/efont/dist/shinonome/shinonome-0.9.11p1.tar.bz2 | 95663c95c92ba5765f63ccbdf033eb93b707be01812a989c548db943479c838f | パブリックドメイン（同梱の LICENSE） |
| JFドットJISKan16s 1.00.20150527 | `jfdotfont-20150527.zip` の `JF-Dot-jiskan16s.ttf` | [自家製ドットフォント](http://jikasei.me/font/jf-dotfont/) のダウンロード（https://ftp.iij.ad.jp/pub/osdn.jp/users/8/8542/jfdotfont-20150527.zip） | 8e574faaaa7e27294aec66464aaf427447b2f0ccec8fe00c50e69bbf509f14b1（zip） | パブリックドメイン（配布ページと、フォントの name テーブル） |

ライセンスの文は `app/src/main/assets/licenses/fonts.txt` にあり、アプリの「このアプリについて」で見られます。

## 何をどこに使うか

- 全角: 東雲の `shnmk16.bdf`（JIS X 0208）を、JIS の区点のまま置きます。東雲に無い所（13 区の NEC 特殊文字、89〜92 区の NEC 選定 IBM 拡張文字）は、Shift JIS を MS932 で Unicode にして、JFドットJISKan16s の中にある 16 ドットのビットマップ（EBDT、16ppem）をそのまま使います。TTF の輪郭は描かず、TTF も同梱しません。
- 半角: 東雲の `shnm8x16r.bdf`（JIS X 0201。5C は ¥）を使います。7C と 7E は、実機の漢字 ROM に合わせて、`shnm8x16a.bdf` の ¦（A6）と ~（7E）にします。
- どちらにも無い字（89〜92 区のまれな漢字と、∑・￤ の 86 字）は、`override/` の手直しした字を使います（下の「手直しした字」）。今は 86 字すべてそろっているので、アプリが端末のフォントで描く字はありません。
- 字は拡大・縮小せず、16 ドットのまま置きます。

## 作り方

Python 3 と fontTools・Pillow（`pip install fonttools pillow`）が要ります。

```
tar -xjf shinonome-0.9.11p1.tar.bz2
unzip jfdotfont-20150527.zip JF-Dot-jiskan16s.ttf
python mkfont16.py build shinonome-0.9.11 JF-Dot-jiskan16s.ttf ../../app/src/main/assets/font16.bin
```

区ごとに、どのフォントの字を入れたか（差し替えた字は override）を表示します。今の `override/` で作ると、東雲 6,746 字・JISKan16s 371 字・差し替え 219 字、287,993 バイトで、SHA-256 は 6bcffcf18e1238dce7568226fde7c3a31c6e980f25cc93e698d7824249afcca4 です（FontGlyphs.VERSION 3）。`override/` の字を使わずに上の 2 つだけから作ると（`--no-override`）、東雲 6,879 字・JISKan16s 371 字、287,993 バイトで、SHA-256 は bd6e8007d6039a1ba80c17be6bce3a114d7aa4d80070be384f0e9e642ea3e768 です（fontTools 4.68.0）。

ファイルの形は `mkfont16.py` の先頭に書いてあります。

## 字を直す（差し替え）

字ごとに形を直せます。直した字は `override/` に「編集用の PNG」と「一覧の CSV」の組で置き、`build` のときに東雲・JISKan16s・端末のフォントより優先して入ります。

1. 直したい字を書き出します。字は区点（`89-15`）、半角の文字コード（`H7C`）、Unicode（`U+4EFC`）、文字そのもの（`飴餅`）、一覧のファイル（`@一覧.txt`）で指定します。

   ```
   python mkfont16.py export ../../app/src/main/assets/font16.bin 作業/食偏 飴 餅 餌
   ```

   `作業/食偏.png`（編集用）、`作業/食偏.csv`（一覧）、`作業/食偏_view.png`（4 倍に拡大した見本）ができます。font16.bin に無い字（今は端末のフォントで描いている字）は、空白のマスになります。

2. 編集用の PNG を、画像ソフトで拡大して直します。
   - 1 字は 16×16 のマス（半角は左の 8×16）で、1 画素がそのまま 1 点です。黒（#000000）が点、白（#FFFFFF）が空きです。
   - マスの間の灰色（#808080）の線と、半角のマスの右半分の灰色は、そのままにしてください。
   - ほかの色（ぼかしの中間色など）や透明な画素、ずれた線があると、`build` はどの字のどの点かを表示して止まります。字を描かずに空白のまま残したマスでも止まります。
   - 漢字は東雲に合わせて、左端の 1 列を空け、横は 1〜15 列、縦は 0〜15 段に収めます。

3. PNG と CSV を、同じ名前のまま `override/` に置きます。CSV の「作り方」の列には、元にした字や直した所を書いておきます（`build` は「マス」と「区点」の列だけを読みます）。同じ字が 2 つのファイルにあると止まります。

4. `build` で font16.bin を作り直します。font16.bin が変わったら、今後も必ずアプリの `FontGlyphs.VERSION` を上げ、`versions.txt` に新しい版と font16.bin の SHA-256 の行を足します（上げないと、もう作った font.tmp が作り直されません）。アプリのビルドのとき（`checkFont16`）に、font16.bin の SHA-256 が今の版の行と合わないと止まります。

`override/` の字は、持ち主や Claude Code が東雲・JISKan16s を元に作ったもので、東雲と同じくパブリックドメインとして扱います。

## 手直しした字（override/）

| ファイル | 字の数 | 中身 |
| --- | --- | --- |
| `b_ibm86` | 86 | 東雲にも JISKan16s にも無い字（13 区の ∑、89〜92 区のまれな漢字、IBM の異体字、￤）。東雲・JISKan16s の字の部品を組み合わせたり点を直したりして作り、持ち主が直した |
| `c_jis2004` | 133 | JIS X 0213:2004 で例示字形が変わった字（飴・餅・しんにょうの字など）を改正後の形にしたもの。JISKan16s の 2000 年版から 2004 年版への違いを東雲の字に当てた下書きを、持ち主が直した。東雲のままにすると決めた字は入れていない |

字ごとの作り方は、それぞれの CSV の「作り方」の列にあります。
