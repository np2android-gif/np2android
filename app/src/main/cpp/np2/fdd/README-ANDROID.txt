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
  CD-ROM イメージは np2kai の diskimage/ から取り込んだコードで開く (FDD・HDD と同じ版 5939e0c6)。
  取り込んだファイル: diskimage/cddfile.c・cddfile.h、diskimage/cd/cdd_iso・cdd_cue・cdd_ccd・cdd_mds (.c/.h)
  取り込んでいないもの: cdd_nrg (.nrg は先送り)、cdd_real (実ドライブ)、cdd_libcdio、np2kai の fdd/sxsicd.c
  ATAPI・CD-DA (cbus/atapicmd.c・cbus/ideio.c) と sxsi.c/sxsi.h は xnp2 のまま (np2kai の ideio.c は
  構造体が大きくなり、ステートセーブの互換性がなくなるため)。
  ただし cbus/ideio.c は 1 か所だけ変えた (/* ANDROID-SAF */ の印あり): 知らない IDE コマンドを受けたとき、
    panic (アプリが終了する) せず、ほかのコマンドと同じ cmdabort() でアボート (ERR・ABRT 04h、割り込みあり) を
    返す。FreeDOS(98) の PATACD.SYS はドライブを探すときに NOP (00h) を送り、そこでアプリが終了していた。
    NOP は ATA の仕様でも必ずアボートで返すコマンド。使わなくなった panic() は #if で外している。
  fdd/sxsicd.c を中継ぎに書き替えた (xnp2 の元のコードは #else 側に残している):
    拡張子で np2kai の読み取りを選び (.cue・.ccd・.cdm・.mds、それ以外は ISO/生イメージ)、開いたあと
    np2kai のトラック表を xnp2 の形 (種類・番号・開始位置) に変換して ATAPI・CD-DA に渡す。
    CD-DA の 1 セクタ読み (sxsicd_readraw) は np2kai と同じ計算で、np2kai では ideio.c にある
    CUE の PREGAP (イメージに実体の無いセクタ) を除く処理もここで行う。
  fdd/sxsicd.h: トラック表の型を _CDTRK_NP2 (xnp2 の形) と _CDTRK_KAI (np2kai の形) に分けた。
    np2kai のファイルは NP2KAI_CDTRK を定義してコンパイルし (CMakeLists.txt)、_CDTRK が np2kai の形になる。
  読める形式 (拡張子で決める。np2kai と同じ)
    .iso・生イメージ: 2048・2352・2448 バイト/セクタ (ファイルの大きさで判定。xnp2 と違い "CD001" は見ない)
    .cue : 最初の FILE ... BINARY だけを使う (WAVE や複数ファイルは np2kai も未対応)。MODE1・MODE2・AUDIO・PREGAP
    .ccd・.cdm : 同じ名前の .img (2352 バイト/セクタ)。.sub は使わない
    .mds : 同じ名前の .mdf (AUDIO と MODE1 のトラック。サブチャンネル付きの 2448 バイト/セクタも可)
  np2kai のファイルへの変更 (/* ANDROID-SAF */ の印あり)
    cddfile.c: Windows 用の img_dosio.h を読まない。xnp2 の SXSIDEV に無い cdflag_ecc (ECC の状態) を使わない。
               1 トラックのときは形式が決めたセクタの大きさで数える (2048 から順に試すと、2352・2448 バイト/
               セクタでセクタ数が 128 の倍数のとき 2048 で割り切れてしまい、セクタ数を誤る)
    cddfile.h: _CDINFO のトラック表を _CDTRK_KAI と名前で指定し、xnp2 の形の表 (np2trk) を足した
    cdd_mds.c: xnp2 に無い INT32 を定義。64 ビットの値 (packed 構造体の中) を 1 バイトずつ読む
  ドライブは IDE のセカンダリ・マスター (sxsi のドライブ番号 0x02) で、IDE が有効なら常につながっている。
  目次ファイル (.cue・.ccd・.cdm・.mds) が名前で開く本体 (.bin・.img・.mdf) は、port/android_safio.c で
  目次ファイルと同じ ID に付けた付属ファイルとして開く。
  入れ替えは port/np2bridge.c の cdset() で行う。sxsi_devopen() は前のイメージを閉じないので、必ず
  sxsi_devclose() してから開く。CD のパスは np2.cfg に保存されないので、アプリの設定に保存し、起動時に
  sdl2/np2.c から np2bridge_startupcd() で入れ直す (/* ANDROID-SAF */ の印あり)。
  ゲスト側の例 (MS-DOS 6.2): CONFIG.SYS に DEVICE=NECCDD.SYS /D:CD101 と LASTDRIVE=Z、
  起動後に MSCDEX /D:CD101 /L:Q
  FreeDOS(98) では PATACD.SYS と SHSUCDX.COM で動くことを確かめている。

ステートのロードとディスク (statsave)
  ロードはセーブ時のディスクを開き直す (statsave_load の sxsi_alltrash のあと flagload_sxsi)。
  ロード前の確認 (statflag_checkpath) は、セーブ時のファイルが無いか更新日時が変わったときだけ
  STATFLAG_DISKCHG を返す。Android では次のように変えている (/* ANDROID-SAF */ の印あり)。
  - statsave.c/h: statsave_diskcb を足し、statflag_checkpath がディスクを 1 台調べるたびに
    名前・セーブ時のパス・結果 (変わっていない / 書き換えられた / 見つからない) を渡す
  - port/np2bridge.c の checkdisks(): それを今開いているディスクと比べ、書き換えられた・見つからない
    ディスクがあるか、HDD がつながっているものと違えば確認を出す (Kotlin の StateDiskCheck が文にする)。
    スロットのロードは EV_STATE_CONFLICT、起動時のレジュームは EV_RESUME
  - sdl2/np2.c の flagload(): レジュームの確認を np2 の menumbox から np2bridge_resume() に替えた。
    エミュレータスレッドは Kotlin のダイアログの答え (ロード / ロードせず起動 / 終了) を待つ
  - ロードに成功したら np2bridge.c の syncdisks() で np2.cfg の HDD を実際に開いたファイルに合わせる
    (メニューの表示と次のリセットがロードした状態と一致する)

ライセンス
  hdd_vhd.h の先頭に「license: same as xnp2 (under the 2-clause BSD)」(lpproj 氏・SimK 氏) とある。
  ほかのファイルには個別の表記がなく、np2kai 全体のライセンス (diskimage/README-ANDROID.txt を参照) が
  適用される。
