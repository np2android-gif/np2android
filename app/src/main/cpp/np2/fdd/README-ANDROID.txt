fdd/ の HDD イメージの処理 (sxsihdd)
====================================

np2kai (https://github.com/AZO234/NP2kai) の fdd/ から、HDD イメージの形式を扱う部分を取り込んだ。
取り込んだ版: wx_alpha ブランチ 5939e0c6d5985c4c08fc70f289a83290e5d3e6f7 (diskimage/ と同じ版)

取り込んだファイル
  sxsihdd.c / sxsihdd.h / hdd_vhd.h / hdd_vpc.h
取り込んでいないもの
  sxsihdd_nvl.c (外部ライブラリ NVL.DLL を読み込む仕組みで、Android では使えない)
  sxsi.c / sxsi.h は xnp2 のまま (np2kai 版は構造体が変わり、ステートセーブの互換性がなくなるため)

増えた形式 (形式は np2kai と同じく拡張子で決める)
  SASI/IDE: .slh (SL9821)、.vhd (VirtualPC の固定・可変サイズ)
  SCSI    : .nhd・.hdi、.hdn・.hds (RaSCSI のベタ)、.vhd
  ただし、いまの Android 版は IA-32 (PC-9821) 構成で SUPPORT_SCSI を定義していないので、
  SCSI のドライブは使えない (fdd/sxsi.h の SCSIHDD_MAX が 0)。

Android 向けの変更 (np2kai を更新して取り込み直すときに、同じ変更を入れ直すこと)
  - 変更箇所には /* ANDROID-SAF */ の印を付けている
  - sxsihdd.c: np2cfg.rascsi92 (この版の np2cfg には無い) を参照せず 0 に固定した
    (.hdn を 1 トラック 25 セクタとして読む。np2kai でも既定は 0)
  - hdd_vhd.h: xnp2 の common.h に無いビッグエンディアンのマクロ (LOADMOTOROLA*・
    STOREMOTOROLA*) を np2kai の common.h から写して定義した
  - 取り込んだファイル以外の変更
    port/compiler.h  : SUPPORT_VPCVHD と MIN を定義
    common/strres.c/h: str_vhd・str_slh・str_hdn・str_hds を追加
    sdl2/dosio.c/h   : file_sync (fflush と fsync) を追加 (可変サイズの VHD が使う)

ライセンス
  hdd_vhd.h の先頭に「license: same as xnp2 (under the 2-clause BSD)」(lpproj 氏・SimK 氏) とある。
  ほかのファイルには個別の表記がなく、np2kai 全体のライセンス (diskimage/README-ANDROID.txt を参照) が
  適用される。
