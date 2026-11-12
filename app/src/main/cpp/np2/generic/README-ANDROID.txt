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
hostdrv.c・hostdrvs.c は xnp2 のまま。NP21/W の hostdrv (新方式、長い名前の 8.3 名の割り当て、
Windows 9x/NT 用) は取り込んでいない。

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
