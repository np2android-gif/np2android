bios/ (代わりの BIOS)
=====================

xnp2 の bios をそのまま使っている。Android 向けに変えたところを書く。
変更箇所には /* ANDROID-BIOS */ の印を付けている。

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
