io/ の I/O ポートの振り分け (iocore)
====================================

Android 向けの変更 (変更箇所には /* ANDROID-IO */ の印を付けている)

I/O ポートを 16 ビットで見分ける (np2cfg.sysiomsk)
  - xnp2 の iocore_build は、ポートの上位バイトの bit2・3 (0x0c00) が 0 なら、上位バイトが何であっても
    本体の機器 (PIC、DMA、PIT、キーボード、プリンタ、CPU のリセットなど) の表に振り分ける。
    例えば 9100h は 00h (PIC)、0140h は 40h (プリンタ) に届く。古い PC-98 の実機の動き。
  - Windows 2000 の GUI セットアップは、ネットワークカードを探すときに ISA のポートを広く読み書きする。
    9100h に 55h を書くと xnp2 では PIC の初期化 (ICW1) になり、STOP 0xD1 から自動の再起動になって
    セットアップが頭からやり直しになっていた (Q5 指示書 9章)。
  - NP21/W rev37 で足された SYSIOMSK (NP2kai の iocore.c にもある) と同じく、np2cfg.sysiomsk を
    足した。iocore_build で !(i & (0x0c | (np2cfg.sysiomsk >> 8))) の上位バイトだけを本体の機器の表にする。
      0xff00: 本体の機器は上位バイト 0 のポートだけ (PC-9821、NP21/W の既定)。この版の既定
      0x0000: 今までの xnp2 と同じ (上位バイトの bit2・3 だけを見る)
    取り込み元: NP21/W rev107 / NP2kai の io/iocore.c (np2 と同じライセンス、MIT 相当)。
    FDC (90h・92h・C8h・CCh など) は元から「全部の上位バイトに共通の表」に付けているので、
    どちらの設定でも上位バイトに関係なく届く (NP21/W も同じ)。
    iocore_attachout・iocore_attachinp で個別に付けたポート (0430h、0640h、MPU の E0D0h、
    音源のポートなど) は、表を作った後に上位バイトごとの表へ付けるので、どちらの設定でも届く。
  - 設定の正は Kotlin の Settings (「I/O ポートを 16 ビットで見分ける (PC-9821)」、既定オン) で、
    np2.cfg には保存しない (ホストドライブと同じ)。Np2Native.setSysIoMask で port/np2bridge.c に渡し、
    np2bridge_sysiomsk() が np2cfg.sysiomsk に反映する (起動時は sdl2/np2.c の initload の後、
    動作中はエミュレータスレッドのイベントで)。
  - I/O の表は起動・リセット・ステートロードで作り直すので、切り替えはその時から効く。
    設定画面の説明に「切り替えたら、ゲストをリセットしてください」と書いている。
    np2cfg はステートに入らないので、ステートの構造は変わらない。
