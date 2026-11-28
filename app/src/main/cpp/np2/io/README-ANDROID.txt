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


FDC (fdc.c・fdc.h) と DMA の番地の上位 (dmac.c)
================================================

Windows NT/2000 の FD ドライバは、FDC と DMA を直接動かす (DOS は互換 BIOS の INT 1Bh を通るので、
FDC のポートも DMA も使わない)。xnp2 のままでは次の 3 つのために、Windows 2000 から FD が読めず、
セットアップのブートディスクからも STOP 0x7B で起動できなかった (Q11 指示書 5章)。
  1. DMA の番地の 24〜31 ビット (0E05h〜0E0Bh) が無い。Windows 2000 は FD のバッファを 16MB より上に
     置くので、16MB より下の別の場所に転送していた (読めないうえ、そこにあるメモリを上書きしていた)。
  2. FDC を 94h でリセットしても割り込みを上げない (ドライバは待ちきれずに「準備ができていません」にする)。
  3. READ DATA が EOT・TC で終わったときの結果の C/H/R が実機と違う (ドライバが読み直しを繰り返し、
     メディアを認識できない)。

fdc.c・fdc.h (印 ANDROID-FDC)
  - Neko Project 21/W rev107 の io/fdc.c に置き換えた (「170101 ST modified to work on Windows 9x/2000」
    などの直しが入った版。ライセンスは NP21/W の修正 BSD、assets/licenses/np21w.txt)。
    文字コードは元のまま (Shift_JIS、LF)。
  - 前の fdc.c にあった Android 向けの足し分は、どれも NP2kai から移した SUPPORT_KAI_IMAGES の部分
    (fdcsend_success7 の FD イメージのリザルトコード、NFD r1 の READ DIAGNOSTIC (fdc_diag_nfd1_mode)、
    SCAN EQUAL、fdc_reset での初期化) で、NP21/W の版にも同じ処理で入っている。
    前の版で省いていた fdc_lasttreg (Windows 用、SEEK の後で treg を C に合わせる) は入った。
  - 元からの違い:
    インクルードは "fdd/fddfile.h" (この版の場所。中身は np2kai の diskimage/fddfile.h)。
    シーク音 (SUPPORT_SWSEEKSND・np2cfg.MOTOR の部分) は外した。
    NP21/W で _FDC に足された int_timer・int_stat と、fdc_lasttreg は、_FDC の外の fdcint (_FDCINT) に置いた。
    _FDC の形は前のままなので、ステートの FDC の節は前と同じ大きさ。fdcint はステートの FDCINT の節で
    保存し、この節の無いステート (a18 まで) では fdc_reset の 0 のまま始まる。新しい版で保存したステートは、
    知らない節があるので a18 までの版では読めない。
  - fdc_intdelay (SEEK・RECALIBRATE・リセットの割り込みを 6 フレーム、約 100ms 遅らせて上げる) を、NP21/W と
    同じく pccore.c の pccore_exec の終わりから呼ぶ。エミュレートしている機械のフレームで数えるので、端末の
    速さ・ウェイトの設定・一時停止には左右されない (止めている間は進まない)。
  - diskimage/ が使う fdc の値 (us・hd・C・H・R・N・mf・rpm・crcn・treg・chgreg など) の意味は変わらない
    (NP21/W も同じ np2kai 系の diskimage を使っている)。FDC の SEEK・RECALIBRATE は fdd_seek を呼ばず、
    treg を直接決める (NP21/W と同じ)。BIOS (bios1b.c) は前のまま。

dmac.c (印 ANDROID-FDC、IA-32 構成だけ)
  - 0E05h・0E07h・0E09h・0E0Bh (チャネル 0〜3) に、DMA の番地の 24〜31 ビットを入れる (NP21/W rev107 の
    dmac_oe05)。21h〜27h (バンク、16〜23 ビット) を書いたら 24〜31 ビットを 0 に戻す (NP21/W の dmac_o21)。
  - 番地は今までの adrs (32 ビット) の上位バイトに入るので、ステートの DMAC の節の形は変わらない。
    今までは上位バイトは常に 0 だった。0E05h〜0E0Bh を書かないゲスト (DOS など) では何も変わらない。
  - 他に DMA を使うもの: SASI (IA-32 構成には無い)、PCM 音源 (cs4231、dmac_getdatas で同じ adrs を使う)。
    NT 系のゲストが音源のチャネルに 0E0xh を書けば、そちらも正しい番地になる。IDE は DMA を使わない。
  - NP21/W の dmac.c のほかの違い (1Dh、11h を読んだら TC を落とす、など) は取り込んでいない。


CPU 倍率の上限と桁あふれ (印 ANDROID-CLOCK、Q17)
================================================

xnp2 は CPU 倍率 (np2cfg.multiple・pccore.multiple) を 32 で切っていた。Q17 で上限を 128 に上げ、
アプリの「設定」から選べるようにした。倍率は realclock (= baseclock × 倍率) を通して、いろいろな所の
クロック数の計算に入る。128 倍 (約 315MHz) で 32 ビットからあふれる所を直した。
  - pccore.h: 上限を PCCORE_MULTIPLE_MAX (128) にまとめた。pccore.c の pccore_set、内蔵メニューの
    embed/menu/dlgcfg.c、アプリの「設定」(port/np2bridge.c の getMachineCfg・setMachineCfg) が使う。
  - embed/menu/dlgcfg.c: 内蔵メニューのスライダーは 1〜16 倍のまま。今までは np2.cfg で 32 倍にしていても、
    設定を開いて OK を押すと 16 倍に下がった。スライダーを動かしていなければ np2cfg.multiple を保つようにした。
  - sound/cs4231c.c (2 か所): realclock × 16 を UINT64 で計算する (110 倍からあふれていた)。
  - sound/pcm86c.c pcm86_setnextintr: SINT64 で計算する (111 倍からあふれていた)。nevent の時間は SINT32 で、
    nevent_set は今のスライスの経過分を足すので、結果を 0x40000000 (128 倍で約 3.4 秒) で止めて余裕を残す
    (0x7fffffff で止めると足し算であふれ、すぐに割り込んだ)。越えるのは 128 倍で FIFO が満杯の 4.1kHz
    (約 8 秒先) のときだけ。止めると先に pcm86_cb が来て残りを計算し直すので、割り込みの時刻は変わらない
    (DOS から 86 ボードの PCM を鳴らすテストで確かめた)。
  - cbus/pc9861k.c pc9861_makeclk: realclock × mul2 (最大 22) を UINT64 で計算する (80 倍からあふれていた)。
  - io/gdc_sub.c calc_gdcslavewait: 点の数 × 27648 × 倍率 を SINT64 で計算する。32 倍でも、線が 2,428 点より
    長いとあふれ、GDC の描画の待ちが短くなっていた (今までもあった問題)。
  - 他の所 (pit・pic・nevent_setbyms・mpu98ii など) は 128 倍ではあふれない (Q17 指示書 5.2 の表)。
  - NP21/W (上限 2048 倍) も cs4231 を UINT64 にしているが、コードは取り込んでいない (同じ考えで書いた)。
    NP21/W は pc9861k・pcm86_setnextintr・gdc_sub をそのままにしている。
  - 倍率と拡張メモリは今までどおり pccore (ステートに入る) と np2.cfg にあり、ステートの形は変わらない。
