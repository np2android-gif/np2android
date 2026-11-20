generic/ のホストドライブ (hostdrv)
====================================

作業フォルダ直下の HOSTDRV/ を、ゲストの DOS から \\HOSTDRV\ として読み書きできるようにする。
ゲスト側では np2tool.d88 の HOSTDRV.COM を常駐させる (アプリには同梱しない)。
新版の HOSTDRV.COM (20250803 版以降) は、NP21/W の新方式 (setn_hostdrv) に対応していない
エミュレータでは、旧版と同じ互換方式で動く (np2tool.d88 の HOSTDRV.TXT)。
xnp2 の hostdrv はファイルの削除・名前の変更 (常にアクセス拒否)、フォルダの作成・削除、
空き容量の取得に対応していない。DOS からできるのは読み取り・新規作成・上書きだけ。
そのため設定画面には「削除を許可」を置かず、Kotlin は常に削除不可 (hdrvacc は 1 か 3) を渡す。
8.3 に縮めて同じ名前になるファイルは、readdir で先に返ったものだけが見え、上書きもそちらに効く
(後のものは見えず、触られない)。共有ストレージは大文字・小文字を区別しないので、a.txt と A.TXT は
そもそも同時に置けない。
hostdrv.c・hostdrvs.c は xnp2 のまま。NP21/W の DOS 版 hostdrv (新方式、長い名前の 8.3 名の割り当て)
と Windows 9x 用は取り込んでいない。Windows NT/2000 用は下の「Windows NT 用ホストドライブ」。

Android 向けの変更 (変更箇所には /* ANDROID-HOSTDRV */ の印を付けている)
  - 設定の正は Kotlin の Settings で、np2.cfg には保存しない。Np2Native.setHostDrv で
    port/np2bridge.c に渡し、np2bridge_hostdrv() が np2cfg.hdrvroot / hdrvacc に反映する
    (起動時は sdl2/np2.c の initload の後、動作中はエミュレータスレッドのイベントで)。
    hdrvroot は <作業フォルダ>/HOSTDRV/ (無ければ作る)、無効なら空。
    hdrvacc は NP21/W と同じく 1=読み取りのみ、3=読み書き、7=読み書き・削除
    (xnp2 の hostdrv.c は bit0 を見ていない)。無効にするとマウント中でも hostdrv_reset で外す。
  - sdl2/dosio.c: file_list1st / file_listnext / file_listclose (POSIX 側)
    FLISTH を DIR と列挙先のパスを持つ構造体へのポインタにし、stat をフルパスで行う。
    元はファイル名だけで stat していたので、カレントディレクトリ以外ではサイズ・日付が取れなかった。
    ディレクトリの判定は d_type ではなく st_mode で行う。np2 内蔵メニューのファイル選択
    (embed/menu/filesel.c) も同じ関数を使う。
  - hostdrv.c (最小限の修正。NP21/W・np2kai の hostdrv に置き換えるときは、その版でも
    直っているかを確かめる。NP21/W rev107 ではどちらも直っている)
    read_data / write_data / close_file: ハンドルが無効 (ステートロードで開き直せなかった
      ファイル) なら何もしない。元は fseek(NULL)・fclose(NULL) で落ちた。
    hostdrv_sfload: 検索の一覧 (flist) が無ければ作る。元は DIR の途中でセーブしたステートを、
      検索していないときにロードすると、ロード全体が失敗した。
  - statsave.c (hostdrv を置き換えても残す)
    statsave_load の初めに hostdrv_reset を呼ぶ。開いているファイルを閉じ (元は漏れた)、
      hostdrv の節が無いステートをロードしたときに今のマウントが残らないようにする。
    statsave_load の終わりで、ホストドライブが無効 (hdrvroot が空) なら hostdrv_reset を呼び、
      ステートのマウント状態を戻さない。


Windows NT 用ホストドライブ (hostdrvnt)
========================================

Windows NT/2000 から HOSTDRV/ をドライブ (ふつうは Z:) として使う。ゲストには NP21/W の
「Neko Project II ホスト共有ドライブドライバ for WinNT」(hstdrvnt) を入れる。

取り込み元: Neko Project 21/W rev107 (np21w-src-rev107) の generic/
  hostdrvnt.c・hostdrvnt.h・hostdrvntdef.h (Shift_JIS、LF)、hostdrvwincompat.c・hostdrvwincompat.h (ASCII)。
  文字コードは元のまま。ライセンスは NP21/W の LICENSES/LICENSE.TXT (3 条項 BSD、
  Copyright (c) 1999-2025, NP2 developer team)。
  hostdrvwincompat.c は、Windows 以外で Win32 のファイル API の代わりをする層 (NP21/W の X11・macOS 版も使う)。
  この 2 つの .c だけ -std=gnu11 でビルドする (UTF-16 の文字列 u"…" を使う。CMakeLists.txt)。
  port/compiler.h で SUPPORT_HOSTDRVNT を定義し、TCHAR を char にする (IA-32 構成だけ)。

Android 向けの変更 (変更箇所には /* ANDROID-HOSTDRV */ の印を付けている)
  - hostdrvnt.c
    hostdrvNT_updateHDrvRoot: ルートの区切りを「\」にそろえる。ゲストのパスは「\」区切りになり、
      ルートと文字列のまま比べるので、絶対パスのルート (「/」区切り) のままだと何も開けなかった。
    ゲストのメモリの読み書き: cpu_kmemory* を、先にページ表を見る形に置き換える (cpu_lmemory_probe。
      i386c/ia32/paging.c に足した)。元はページが無いと I/O の処理の途中でページフォールトになり、
      siglongjmp で抜けて確保したメモリが漏れ、ファイルの表が中途半端に残った。要求の初めに、
      ドライバが渡したデータ・要求の内容・結果を書く場所を確かめ、だめなら何もせずに返す。途中で
      読み書きできなかったら結果をエラーにする。hostdrvNT_memread / memwrite は範囲を先に確かめて、
      ページ単位でまとめて読み書きする。
    READ / WRITE: 要求の長さのまま確保していた (最大 4GB) のをやめ、相手の範囲を先に確かめてから
      HOSTDRVNT_IOCHUNK (1MB) ずつ読み書きする。Windows 2000 の copy が送る長さは最大 65,024 バイト
      だった (100MB のファイルで確認) ので、ふつうは分かれない。返す長さは実際に読み書きできた長さ。
    ファイルの表 (65536 件、arm64 で 6MB): 構造体の配列をポインタにし、NT 版を初めて有効にしたとき
      (hostdrvNT_bind) に確保する。元は起動・リセットのたびに全部を 0 で埋め、使わない人も 6MB 使っていた。
      表が無い間は、要求を何もせずに返す。
    空き容量: ドライバの UseRealCapacity (既定 0、容量 2GB・空き 1GB の仮の値) に関係なく、HOSTDRV のある場所の
      実の値 (hostdrvwincompat.c の GetDiskFreeSpaceExW、statvfs) を返す。取れなければ仮の値。
  - hostdrvwincompat.c
    CreateFileW: open に O_NOFOLLOW を足す (確かめてから開くまでの間にシンボリックリンクに替えられても辿らない)。
    SetFileTime: 時刻が 0 (とすべて 1) なら変えない (Win32 / FILE_BASIC_INFORMATION と同じ)。元は
      Windows 2000 から写したファイルの日付が 1601-01-01 になった。
  - hostdrvsfn.h・hostdrvsfn.c
    hostdrvnt.c が使う短い名前 (8.3 名、NAME~1.TXT の形)。NP21/W rev107 の generic/hostdrvs.c の先頭から
    hostdrvs_lookuplongnamecached までを写した (Shift_JIS のまま)。xnp2 の hostdrvs.c (DOS 版) と関数名・
    型名がぶつかるので別のファイルにし、元の hostdrvs.h・hostdrv.h にあった型をファイルの中に置いた。
    DOS 版だけが使う部分 (s_hddroot、GetHostRootPath の宣言、IsMatchFcb、AddFileToList) は除いた。
    DOS 版 (xnp2 の hostdrv.c・hostdrvs.c) の短い名前は今までどおり (切り詰め、重なると片方だけ)。
    ディレクトリの変更の監視 (DOSIO_HAS_DIRMONITOR) は無いので、Android 側で増やしたファイルの短い名前は、
    NP21/W の Windows 以外の版と同じく、キャッシュが捨てられるまで古いことがある。
  - hostdrvnt.c の hostdrvNT_sfn_wtooem: 短い名前の側に渡すパスの「\」を「/」にする (dosio が開けるように)。
  - sdl2/dosio.c・dosio.h: FLINFO に shortpath (常に空)、file_getinfo・file_getshortname (常に失敗)・
    file_islink・file_infoislink を足す (NP21/W の sdl2/dosio の Windows 以外の側と同じ)。
  - pccore.h: np2cfg に hdrvenable (ホストドライブ全体。NT 版は呼び出しのたびに見る) と hdrvntenable
    (NT 版のポートを付けるか。リセットで効く) を足す。名前は NP21/W と同じ。初期値の並びの後ろに置く。
  - pccore.c: 起動・終了・リセットで hostdrvNT を初期化・後始末する。CPU だけのリセット (Windows の再起動)
    でも hostdrvNT_reset を呼ぶ (NP21/W と同じ)。
  - io/iocore.c: bind の表に hostdrvNT_bind (ポート 07ECh・07EEh。上位バイト 07 の拡張表に付くので、
    sysiomsk の値に関係なく同じ)。
  - port/np2bridge.c: Kotlin の「Windows NT/2000 からも使う」を HOSTDRV_NT で受け、np2bridge_hostdrv が
    hdrvenable・hdrvntenable を入れて hostdrvNT_updateHDrvRoot を呼ぶ (権限も次の要求から効く)。
    無効にしたときは hostdrvNT_reset で開いているファイルを閉じる。

  - ステート (statsave.tbl・statsave.c。形は NP21/W の HOSTDRVNT 節の版 0 のまま)
    NT 版を使っているとき (hdrvntenable で、表を確保してある) だけ HOSTDRVNT の節を書く。a17 までの版は
    知らない節があるとステートを読めないので、使っていなければ今までどおり行き来できる。
    statsave_load の初めに hostdrvNT_reset を呼ぶ (節が無いステートでも、前の状態が残らない)。
    終わりで NT 版を使っていなければ、もう一度 hostdrvNT_reset を呼ぶ (DOS 版の #1 と同じ)。
    hostdrvNT_sfload: 読み込みの失敗、件数と番号の範囲 (元は負の番号を確かめず表の外に書けた)、名前の長さと
      終端を確かめ、だめならロード全体を失敗にする。ホストのパスは読み捨て、ゲストのパスから作り直す (元と同じ)。
      表を確保していなければファイルを戻さない。
    NP21/W と同じく、「閉じたら消す」予約はステートに保存せず、セーブした時点で外れる。

設定と同梱のドライバ (Kotlin)
  - 「Windows NT/2000 からも使う」(hdrvntenable)、「削除・名前の変更を許可」(hdrvacc の 4。書き込みを許可して
    いるときだけ渡す。オンにするときに確かめる)。削除は NT 版だけに効く (DOS 版は削除に対応していない)。
  - ゲスト用のドライバは ISO (assets/hostdrvnt/hstdrvnt.iso) で同梱し、CD-ROM のメニューから入れる
    (作業フォルダの hostdrvnt/ に写す)。a18 の時点では Windows 2000 から FD が読めなかったので FD (hstdrvnt.hdm) にはしなかった (a19 で読めるようになったが、CD のままにしている)。
    ISO の中身と作り方は tools/hostdrvnt_iso/README.md。
