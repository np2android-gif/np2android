diskimage/ (FDD・CD-ROM イメージの処理)
=========================================

np2kai (https://github.com/AZO234/NP2kai) の diskimage/ のうち、FDD と CD-ROM に関するものを取り込んだ。
取り込んだ版: wx_alpha ブランチ 5939e0c6d5985c4c08fc70f289a83290e5d3e6f7 (2026-09-05)

取り込んだファイル
  fddfile.c / fddfile.h / img_common.h / img_strres.c / img_strres.h
  fd/ 以下すべて (d88, xdf (ベタ・FDI), dcp/dcu, nfd, vfdd (.fdd), bkdsk (.hdb など))
  CD-ROM: cddfile.c / cddfile.h、cd/ の cdd_iso・cdd_cue・cdd_ccd・cdd_mds (.c/.h)
    (使い方と変更点は fdd/README-ANDROID.txt の「CD-ROM (sxsicd)」を参照)
取り込んでいないもの
  cd/cdd_nrg (.nrg は先送り)・cdd_real (実ドライブ)・cdd_libcdio、win9x/

xnp2 の fdd/fddfile.c・fdd_d88.c・fdd_xdf.c・d88head.h は削除し、fdd/fddfile.h はこちらを
読み込むだけのファイルにした。SUPPORT_KAI_IMAGES は port/compiler.h で定義している。

Android 向けの変更 (np2kai を更新して取り込み直すときに、同じ変更を入れ直すこと)
  - 変更箇所には /* ANDROID */ または /* ANDROID-SAF */ の印を付けている
  - fddfile.c: np2cfg.fddfile (この版の np2cfg には無い) への書き込みを外した
  - fddfile.c: fdd_set() で拡張子からベタと決まったとき、fddsniff_android.c の
    fdd_sniff_android() でヘッダーを調べ、NFD・VFDD・D88 ならその形式で開く
    (中身が D88 や NFD の .hdb などを開くため)。fddsniff_android.c/.h はこの移植で追加した
    ファイルで、np2kai には無い
  - fd/fdd_d88.c / fdd_d88.h: fddd88_flush() を追加 (バックグラウンドへ移るときにトラック
    バッファを書き戻す。port/taskmng.c から呼ぶ)
  - io/fdc.c・bios/bios1b.c: np2kai の SUPPORT_KAI_IMAGES の部分だけを移植した。
    fdc.c は丸ごと置き換えていない (np2kai の _FDC は 8 バイト大きく、ステートセーブの
    互換性がなくなるため)

ライセンス
  取り込んだファイルには個別のライセンス表記がなく、np2kai 全体のライセンスが適用される。
  - LICENSE-NP2kai.txt : MIT License (Copyright (c) 2017 AZO)
  - LICENSE-NP2.txt    : 3 条項 BSD (Copyright (c) 1999-2025, NP2 developer team)
  アプリ内の「ライセンス」画面にも同じ内容を表示している。
