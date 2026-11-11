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
  SCSI は port/compiler.h で SUPPORT_SCSI を定義して有効にしている (IA-32 構成で IDE と併用。
  np2kai の IA-32 構成と同じ)。SCSI のディスクをつないだときだけ SCSI ボード (BIOS は d2000h) が
  有効になる。ステートセーブには SCSI の節が増えるが、SCSI の節が無い古いセーブもロードできる。
  注意: SCSI BIOS (d2000h〜) はSCSI のディスクをつないだときだけ現れる。ゲスト側で EMM386 などの
  UMB の範囲を固定で設定していると、ぶつかることがある (そのときは d2000h〜d5fffh を UMB から外す)。

Android 向けの変更 (np2kai を更新して取り込み直すときに、同じ変更を入れ直すこと)
  - 変更箇所には /* ANDROID-SAF */ の印を付けている
  - sxsihdd.c: np2cfg.rascsi92 (この版の np2cfg には無い) を参照せず 0 に固定した
    (.hdn を 1 トラック 25 セクタとして読む。np2kai でも既定は 0)
  - hdd_vhd.h: xnp2 の common.h に無いビッグエンディアンのマクロ (LOADMOTOROLA*・
    STOREMOTOROLA*) を np2kai の common.h から写して定義した
  - 取り込んだファイル以外の変更
    port/compiler.h  : SUPPORT_VPCVHD・SUPPORT_SCSI と MIN を定義
    common/strres.c/h: str_vhd・str_slh・str_hdn・str_hds を追加
    sdl2/dosio.c/h   : file_sync (fflush と fsync) を追加 (可変サイズの VHD が使う)

CD-ROM (sxsicd)
  CD-ROM は xnp2 のコード (sxsicd.c・cbus/ideio.c・cbus/atapicmd.c) をそのまま使っている。np2kai からは
  取り込んでいない (np2kai の ideio.c は構造体が大きくなり、ステートセーブの互換性がなくなるため)。
  ドライブは IDE のセカンダリ・マスター (sxsi のドライブ番号 0x02) で、IDE が有効なら常につながっている。
  読める形式は iso (2048 バイト/セクタ)、データだけの生イメージ (2352 バイト/セクタ)、
  .bin が 1 つの .cue (音楽トラックを含む。CD-DA の再生は xnp2 の ideio.c)。
  sxsicd.c の変更は 1 か所: .cue の INDEX 00 (プリギャップ) をトラックとして数えない
  (xnp2 は INDEX の行をすべてトラックにするので、プリギャップのある .cue で TOC のトラックが重複する)。
  .cue が名前で開く .bin は、port/android_safio.c で .cue と同じ ID に付けた付属ファイルとして開く。
  それ以外のコアは変更していない。入れ替えは port/np2bridge.c の cdset() で行う。sxsi_devopen() は前のイメージを
  閉じないので、必ず sxsi_devclose() してから開く。CD のパスは np2.cfg に保存されないので、アプリの設定に
  保存し、起動時に sdl2/np2.c から np2bridge_startupcd() で入れ直す (/* ANDROID-SAF */ の印あり)。
  ゲスト側の例 (MS-DOS 6.2): CONFIG.SYS に DEVICE=NECCDD.SYS /D:CD101 と LASTDRIVE=Z、
  起動後に MSCDEX /D:CD101 /L:Q

ライセンス
  hdd_vhd.h の先頭に「license: same as xnp2 (under the 2-clause BSD)」(lpproj 氏・SimK 氏) とある。
  ほかのファイルには個別の表記がなく、np2kai 全体のライセンス (diskimage/README-ANDROID.txt を参照) が
  適用される。
