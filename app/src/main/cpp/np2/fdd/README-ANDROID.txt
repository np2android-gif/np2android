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
  cbus/ideio.c の ideio_i430 (/* ANDROID-IDE */ の印あり): ポート 430h を読んだとき、選んでいるバンク
    (432h で選んだチャネル) のスレーブ (drv[1]) があれば bit6 を立てて返す。Windows 2000 の IDE ドライバは
    bit6 が 1 のときだけスレーブを探し、無いとスレーブの起動ディスクが見えず STOP 0x7B (0xC0000034) になる。
    np2kai (wx_alpha 5939e0c6) の ideio_i430 から一部だけ取り込んだ。np2kai は 430h でバンクの代わりに 0/1 を
    返し、その値を ideio.bank に書き戻すが、ここでは戻り値の下位ビット (& 0x81) と bank への書き戻しは
    xnp2 のまま。432h の読み出しも変えていない。ideio の構造体は変えていない (ステートの互換性はそのまま)。
  cbus/ideio.c の setidentify (/* ANDROID-IDE */ の印あり): IDENTIFY DEVICE で返す HDD のシリアル番号の
    6 文字目 (ワード 12 の下位バイト) を '0' + SXSI のドライブ番号にして、ドライブごとに変える。同じ番号だと
    Windows 2000 は 2 台目の HDD を IDENTIFY したあと使わない (読み出しを一度もしない)。np2kai の setidentify
    にある同じ手当て (「シリアル番号はユニークにしておかないと駄目っぽい」) から。np2kai はワード 12 全体を
    置き換えるが、ここでは文字列に NUL が入らないよう下位バイトだけを変えた。
    np2kai の 0000:05BA (IDE の接続フラグ、NT 4.0 向け) の設定は取り込んでいない。
    np2kai: MIT License, Copyright (c) 2017 AZO
  cbus/ideio.c の書き込み 30h/31h の割り込み (/* ANDROID-IDE */ の印あり): ATA の PIO の書き込みでは、
    コマンドの受付では割り込まず (DRQ を立てて最初のセクタのデータを待つ)、セクタを 1 つ書き終えるごとに
    割り込む。xnp2 は受付で割り込み、最後のセクタを書き終えたあとは割り込まなかった。DOS や BIOS は
    ポーリングするので困らないが、Windows 2000 の IDE ドライバ (atapi.sys) は書き込みの完了の割り込みを待つ
    ので、10 秒でタイムアウトしてリセットと再試行を繰り返し、セットアップの区画の作成で止まっていた。
    受付は writeinit() (割り込まない) に分け、最後のセクタのあとは状態を DRDY・DSC にして割り込む。
    途中のセクタは今までどおり writesec() が次の DRQ と割り込みを出す。NP21/W rev107 の ideio.c
    (writeinit と ideio_w16) と同じ流れにしたが、コードは取り込まず xnp2 の形で書いた。
    ideio の構造体は変えていない (ステートの互換性はそのまま)。
  cbus/ideio.c の READ VERIFY 40h/41h (/* ANDROID-IDE */ の印あり): xnp2 には無く、知らないコマンドとして
    アボートしていた。Windows 2000 のセットアップは NTFS のフォーマットでこれを使うので、すべて失敗して
    「パーティションをフォーマットできませんでした」になっていた。NP21/W rev107 と同じく、データは転送せず
    受け付けてすぐ完了の割り込みを上げる。NP21/W は範囲を見ないが、ここでは開始セクタから個数分
    (0 は 256) がディスクの総セクタ数を越えるときは、読み出し 20h と同じくアボートを返す。
    CD-ROM (ATAPI) に来たときは今までどおりアボート。
  cbus/ideio.c の FLUSH CACHE E7h (/* ANDROID-IDE */ の印あり): xnp2 はすぐに完了の割り込みを上げていた。
    Windows 2000 の GUI セットアップでは、ESENT (\WINNT\security の edb*.log) がログをほぼ空のまま切り替え続け、
    区画が埋まっていた (Q5 指示書 15章: すぐ完了だとログ 165〜533 個、遅らせると 3 個)。NP21/W rev107 と同じく
    BSY を立て、20,000 クロック後 (約 78MHz で 0.25 ミリ秒) に nevent の NEVENT_SASIIO で ideioint() が BSY を解いて
    割り込む。NP21/W rev107 の setdintr2・ideioint2 の考え方を取り込み、次の点は xnp2 の形で書き直した。
    ・イベントが来たら 4 台のうち BSY のドライブを解く (BSY を立てるのは FLUSH だけ)。NP21/W は今選んでいる
      チャネルだけを解くので、待ちの間に 432h でチャネルを切り替えると BSY のまま残る。
    ・nIEN (割り込み禁止) のときもイベントは予約して BSY を解き、割り込みだけ上げない (NP21/W は予約しない)。
    ・SRST (74Ch) と ideio_reset で、待ち中のイベントを取り消す。
    ideioint は statsave.tbl の evtproc に登録した ('eide')。登録しないと待ち中のステートセーブで関数が保存されず、
    ロード後に BSY のまま止まる。表に足しただけなので、今までのステートも読める。ideio の構造体は変えていない。
    SASI と IDE は同時に使わない (pccore で SASI のときは IDE を外す) ので、NEVENT_SASIIO を分け合っても重ならない。
    NP21/W: MIT 相当 (np2 と同じライセンス)
  CD-ROM の名前 (/* ANDROID-IDE */ の印あり):
    cbus/atapicmd.c の INQUIRY (YUIDEBUG の側): 製品名を "CD-ROM DRIVE:251" から "CD-ROM DRIVE:98 " に、
    版を "4.09" から "1.0 " にした。":251" のままだと Windows 2000 の IDE ドライバが NEC の CD チェンジャー
    (ATAPI 2.5) と見なし、デバイスの開始に失敗して (コード 10) ドライブ文字が付かなかった。直したのはこちら。
    cbus/ideio.c の IDENTIFY の型名 (cdrom_model): "NEC CD-ROM DRIVE" を "NEC CD-ROM DRIVE:98" にした。
    こちらは合わせただけで、これだけでは直らない (Q6 指示書 14章の試験 A)。
    どちらも NP21/W rev107 と np2kai (wx_alpha 5939e0c6) の cbus/atapicmd.c・cbus/ideio.c と同じ値。
    MODE SENSE のページ 2Ah は変えていない。DOS の NECCDD・PATACD でも読めることを確かめた。
  CD の入れ替え (/* ANDROID-IDE */ の印あり。Q7 指示書 5〜9章):
    xnp2 はメニューで CD を入れ替えると、その場で閉じて開き直す。Windows 2000 は約 4 秒ごとに
    TEST UNIT READY で CD を確かめ、「媒体が無い」と答えられると 0.2 秒ほどの間にもう 2 回確かめて、
    3 回とも空のときだけ入れ替えに気づく。その場の入れ替えでは空の時が無く、前の CD の中身を見せ続けた。
    そこで、入っていた CD を閉じたとき (cbus/ideio.c の ideio_notify) に IDEDRV の swapwait を
    ATAPI_SWAPWAIT (3) にし、保護モード (VM86 でない) から来た TEST UNIT READY には、新しい CD が
    入っていても swapwait の回数だけ「媒体が無い (3Ah)」と答えてから、今までどおり 28h を返す
    (cbus/atapicmd.c)。swapwait は次のときに 0 に戻る: CD が本当に空のときの TEST UNIT READY
    (空を見せたので、入れたときは 28h だけでよい)、28h を返したとき、DOS (リアルモードか VM86) から
    TEST UNIT READY が来たとき (DOS の NECCDD・PATACD は 28h だけで気づくので、今までどおり)、
    リセット (ideio の初期化で IDEDRV を 0 にする)。CD を入れていなかったところへ入れたときは立たない。
    swapwait は IDEDRV の使っていなかった dmy の名前を変えたもので、構造体の大きさと並びは変わらない
    (ステートの形は a14 と同じ。a14 以前のステートでは 0、a15 のステートを a14 で読むと無視される)。
    np2kai (wx_alpha 5939e0c6) の fdd/sxsi.c の cdchange_* と cbus/atapicmd.c の MEDIA_CHANGE_WAIT が、
    CD を一度取り出して数秒待ってから入れるのと同じ目的を、待ちを作らずに果たすよう書き直した
    (待ちを作る形は、待ちの間のステートセーブと「CD を挿入してください」の窓で Windows 2000 が気づかなかった)。
    センスキーは np2kai と同じ 02h (NOT READY)。
    Windows 2000 の問い合わせ方に合わせたもので、他の保護モードの OS (Windows 95/98、NT 4.0) は未確認。
    入れ替えた直後に一度「準備ができていない」と出ることがあっても、実機でも起こる動きの範囲とみている。
  CD を入れずに起動したとき (/* ANDROID-IDE */ の印あり。Q7 指示書 10章):
    cbus/ideio.c の setidentify は、ファイルが開いていない (SXSIFLAG_READY でない) と失敗を返していたので、
    CD-ROM に CD が入っていないと IDENTIFY PACKET DEVICE が中止になり、Windows 2000 の IDE ドライバが
    CD-ROM ドライブを見つけなかった (ドライブ文字が無く、後から CD を入れても使えない。a14 まで)。
    CD-ROM のときは開いていなくても答えるようにした。CD-ROM の答えはシリアル・型名・固定の値だけで、
    ディスクの形の値は使わない。np2kai (wx_alpha 5939e0c6) の setidentify と同じ条件。
  注意: DOS の HDD の読み書きは、xnp2 の互換 BIOS が HDD のイメージを直接読み書きするので、
    cbus/ideio.c のポートを通らない (DOS の COPY の間に IDE のポートを使うのは、NECCDD・PATACD の
    CD-ROM の ATAPI パケット A0h だけだった)。IDE の HDD のコマンドを変えたときは、DOS ではなく
    Windows 2000 など自前の IDE ドライバを持つ OS で確かめる。
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

SD カードなどが今は開けないとき (Kotlin の DiskImageSource・Np2Activity・Np2Menu。0.86-a16)
  直接オープンのイメージが開けないとき、「削除された」なら今までどおり履歴から外して権限も手放すが、
  「今は開けない」(SD カードが外れている、クラウドにつながらないなど) なら履歴と権限を残す。
  - 例外の種類や問い合わせの結果では見分けられない (どちらも FileNotFoundException、問い合わせも null)
    ので、ファイルのあるボリュームが今つながっているかで見分ける (DiskImageSource.isUnavailable)。
    端末のストレージ (externalstorage) はドキュメント ID の「UUID:」、ダウンロードの raw: はパスの
    「/storage/UUID/」でボリュームを決める。ID から分からないもの (ダウンロードの msf:、メディア) は、
    最後に開けたときに履歴に記録したボリューム (項目「vol」。0.86-a17) で決める。記録も無いもの
    (0.86-a16 以前の履歴で、a17 でまだ一度も開けていない) は、取り外せるボリュームが 1 つでも外れていれば
    「今は開けない」。それ以外の提供元 (クラウドなど) は常に「今は開けない」。
  - 「vol」は、登録できたときの fd の実体のパス (/proc/self/fd のリンク先) から決める (内部は ""、
    取り外せるものは UUID の大文字)。実体のパスの形は Android の版で違うので、/storage/… の形にそろえてから
    見る (DiskImageSource.normalizeRealPath)。Android 10 では externalstorage の fd が /mnt/media_rw/UUID/…、
    ダウンロードの fd が /storage/UUID/…。Android 11 以降ではダウンロードの msf: の fd が
    /mnt/user/<ユーザー>/emulated/<ユーザー>/… (同じ形の /mnt/pass_through・/mnt/installer・
    /mnt/androidwritable も同じに扱う)。分からないパス (クラウドのキャッシュなど) は記録しない。
  - 実体が端末のごみ箱 (.trash-storage の「.trashed-…」) にあるものは、開かずに「削除された」として
    履歴から外す。Android 11 以降は削除したファイルがしばらくごみ箱に残り、msf: の URI では開けてしまうが、
    書いても後で端末が消すため。ごみ箱から戻しても自動では拾い直さない (選び直す)。
    a16 は「vol」を読まずに捨てる (a16 で履歴を保存すると消え、a17 で次に開けたときに記録し直す)。
  - 起動時に「今は開けない」ものがあれば、エミュレータのスレッドを始める前にダイアログを出す
    (Np2Activity.holdForUnavailable)。「終了」なら C のコードを一度も動かさずに閉じるので、レジュームも
    np2.cfg も書かれない。スレッドを止めておくのに SDL の Java 側の中身 (SDLActivity の mHasFocus と、
    それを見てスレッドを始める handleNativeState) に頼っている。SDL を更新したら、SD カードを外して
    起動し、このダイアログが出ている間に SDL_main が始まらないこと (logcat に「Running main function」
    が出ないこと)、「終了」で np2sdl2.sav と np2.cfg が変わらないことを確かめること。
  - 前回の CD-ROM の記録は、今は開けない CD なら起動時に入れに行かず、消さない。
  - 「今は開けない」の印は保存しない。
  - レジュームの確認の差分の行には、C の側 (port/np2bridge.c の addline) が名前のほかに仮想パスの ID も
    入れる (0.86-a17)。「今は開けない」と「履歴にある」は ID で照らす (同じ名前の別のイメージと
    取り違えない)。ID が無いもの (仮想パスでない) は名前で照らす。
  既知の制限
  - 0.86-a16 以前の履歴で、a17 でまだ一度も開けていない msf: などのイメージは、ボリュームの記録が無いので、
    カードを物理的に抜いたまま起動すると履歴から消えることがある (a16 と同じ)。一度開ければ記録される。

同じファイルの二重の挿入 (Kotlin の DiskImageSource・Np2Menu。0.86-a17)
  同じファイルでも、ファイルを選ぶ画面の入口によって URI が違う (「SD カード」なら externalstorage、
  「ダウンロード」「最近」なら msf:) ので、履歴の ID も仮想パスも違うことがある。2 つのドライブに同じ
  ファイルを入れて両方から書くとイメージが壊れるので、FDD・HDD (SASI・IDE・SCSI) の間では、仮想パスが
  違っても fd の実体のパスが同じなら止める (Np2Menu.usedByOtherDrive)。実体のパスは、上の「vol」と同じく
  /storage/… の形にそろえて比べる。fstat の st_dev・st_ino は、
  マウントの見え方で値が変わるかを確かめていないので使っていない。CD-ROM は読むだけなので、今までどおり
  仮想パスが同じときだけ止める。FDD と HDD は拡張子が重ならないので、同じファイルが両方に入ることはない。

ライセンス
  hdd_vhd.h の先頭に「license: same as xnp2 (under the 2-clause BSD)」(lpproj 氏・SimK 氏) とある。
  ほかのファイルには個別の表記がなく、np2kai 全体のライセンス (diskimage/README-ANDROID.txt を参照) が
  適用される。
