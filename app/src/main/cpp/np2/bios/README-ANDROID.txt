bios/ (互換 BIOS)
=====================

xnp2 の bios をそのまま使っている。Android 向けに変えたところを書く。
変更箇所には /* ANDROID-BIOS */・/* ANDROID-SERIAL */ の印を付けている。

0481h (BIOS_FLAG3) の bit6 を立てる (新しいキーボードの印)
  bios.c の bios_reinitbyswitch() で mem[MEMB_BIOS_FLAG3] |= 0x40 とする。biosmem.h に
  MEMB_BIOS_FLAG3 (0x00481) を足した。NP21/W・np2kai も SUPPORT_PC9801_119 のとき同じことをしている
  (np2kai wx_alpha 5939e0c6 の bios.c。np2kai: MIT License, Copyright (c) 2017 AZO)。
  Windows 2000 のセットアップ (NTDETECT) は 0481h の bit3・bit6 でキーボードの種類を決める。
  どちらも 0 だと古い機種のキーボードとして報告され、それは Windows 2000 が知らない種類なので、
  キーボードのデバイスが作られず、テキストモードのセットアップでキーが効かなかった。
  bios_reinitbyswitch() は起動の入口 (BIOSOFST_INIT) で bios_memclear() の後に呼ばれるので、
  リセットのたびに立つ (アプリの起動、メニューのリセット、トリプルフォルトのリセット)。
  レジュームやステートのロードでは、保存したときのメモリの値をそのまま使う。0.86-a9 までに保存した
  ステートでは bit6 が 0 のままで、次にリセットしたときに立つ。

F8E80h+11h の bit4 を立てる (115200bps に対応、0.86-a25)
  bios.c の bios_reinitbyswitch() で mem[0xf8e80 + 0x11] |= 0x10 とする (/* ANDROID-SERIAL */)。
  F8E80h からは PC-9821 の機能フラグで、NP21/W rev107 の bios.c は SUPPORT_RS232C_FIFO のときこの
  ビットを立てる (コメントは「115200bps support flag」。NP21/W: 修正 BSD ライセンス、SimK 氏・
  NP2 developer team)。xnp2 の互換 BIOS は F8E80h〜F8E85h しか書かないので、+11h は 0 のままだった。
  Windows 2000 (PC-98 版) の NTDETECT はこのビットを見て、内蔵の COM1 を FIFO 付き (*nEC1502) と
  報告する。0 だと FIFO なし (*nEC1501) になり、Windows 2000 にはその ID の INF が無いので、
  デバイス マネージャーで COM1 が「その他のデバイス」になり、コード 28 (ドライバが入っていない) に
  なっていた。立てると「通信ポート (COM1)」(serial のドライバ) として入る。
  FIFO のポート (0130h〜013Ah) はこのソースには無い。ドライバは起動時にそこを調べて FFh を読み、
  8251 (30h・32h) だけで動く。COM1 は今までどおり何もつながっていない。
  リセットのたびに立つ。レジュームやステートのロードでは保存したときのメモリの値のままで、
  0.86-a24 までに保存したステートでは次にリセットしたときに立つ。既にインストールした Windows 2000 は、
  次の起動で COM1 が *nEC1502 として入り直す。
