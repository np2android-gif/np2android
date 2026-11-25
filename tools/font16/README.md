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
- どちらにも無い字（89〜92 区のまれな漢字と、∑・￤ の 86 字）は、アプリが端末のフォントで描きます。
- 字は拡大・縮小せず、16 ドットのまま置きます。

## 作り方

Python 3 と fontTools（`pip install fonttools`）が要ります。

```
tar -xjf shinonome-0.9.11p1.tar.bz2
unzip jfdotfont-20150527.zip JF-Dot-jiskan16s.ttf
python mkfont16.py shinonome-0.9.11 JF-Dot-jiskan16s.ttf ../../app/src/main/assets/font16.bin
```

区ごとに、どちらのフォントの字を入れたかを表示します。上の 2 つから作ると、東雲 6,879 字・JISKan16s 371 字、287,993 バイトで、SHA-256 は bd6e8007d6039a1ba80c17be6bce3a114d7aa4d80070be384f0e9e642ea3e768 です（fontTools 4.68.0）。

ファイルの形は `mkfont16.py` の先頭に書いてあります。
