# Windows NT/2000 用ホストドライブのドライバ ISO

アプリに同梱する `app/src/main/assets/hostdrvnt/hstdrvnt.iso` の作り方です。CD-ROM のメニューの
「Windows NT/2000 用ホストドライブのドライバ」で、この ISO を CD-ROM に入れます。
Windows 2000 からは FD が読めないため、ドライバは FD ではなく CD で渡します。

## 中身

`files/` の 6 つを、手を加えずにそのまま入れています。

| ファイル | 取り出し元 | MD5 |
| --- | --- | --- |
| HOSTDRV.INF | NP21/W rev107 のソース zip の `np2tool/hostdrvnt/hstdrvnt.hdm` | 01035571a78001d55e426a55f9fb401c |
| HOSTDRV.SYS | 同上 | b778dc68279dd156d5230447520fb3fe |
| HDRVMNT.EXE | 同上 | c2b3eaa971e3c54edac1fd0db48d8959 |
| HOSTDRV.REG | 同上 | 0e50d26e7604a1e517b9e840d40658d9 |
| README.TXT | 同上 | f1d9c5b5731bbae7e8d5c5aebcf185a1 |
| LICENSE.TXT | NP21/W rev107 のソース zip の `LICENSES/LICENSE.TXT` | 274276b48b0ddd3d4036db957ac8a97b |

- 「Neko Project II ホスト共有ドライブドライバ for WinNT」ver1.5 (SimK)。README.TXT のとおり、
  本体と同じ修正 BSD ライセンスです。README.TXT にはライセンスの本文が無いので、LICENSE.TXT を足しています。
- hstdrvnt.hdm の中のファイルは、作者が配布している hstdrvnt_1_5.zip の中のものと MD5 で一致しました。
  ソース zip の `np2tool/hostdrvnt/` に直接ある hostdrv.inf と readme.txt は、FD の中のものと
  改行などが違うので使いません。
- 入れていないもの: INSTINF.EXE (Windows NT 3.51 用)、DRIVEICO.REG (ドライブのアイコンを変えるおまけ)。

## 作り方 (Windows)

```
python fatx.py <np21w-src-rev107>/np2tool/hostdrvnt/hstdrvnt.hdm extracted
powershell -ExecutionPolicy Bypass -File mkiso.ps1 -Src files -Out ..\..\app\src\main\assets\hostdrvnt\hstdrvnt.iso -Label HSTDRVNT
```

- `fatx.py`: FAT12 の FD イメージ (PC-98 の 1024 バイト セクターも可) のルートのファイルを取り出す。
  取り出したものを `files/` に入れる (LICENSE.TXT はソース zip から写す)。
- `mkiso.ps1`: Windows の IMAPI2 で ISO9660 + Joliet の ISO を作る。ボリューム ラベルは HSTDRVNT。
  ISO には作った日時が入るので、作り直すと ISO の MD5 は変わります (中のファイルは同じ)。

## Windows 2000 での入れ方

CD-ROM のドライブ文字が F: のとき、cmd で:

```
rundll32 setupapi,InstallHinfSection DefaultInstall 132 f:\hostdrv.inf
```

の後、Windows を再起動します。アプリの設定で「ホストドライブを使う」と「Windows NT/2000 からも使う」を
オンにしておくと、Z: (空いている文字のうち後ろから) に HOSTDRV フォルダが見えます。
HOSTDRV.REG は、レジストリの `HKLM\SYSTEM\ControlSet001\Services\hostdrv` にサービスの登録 (種類、自動起動、
`system32\drivers\hostdrv.sys`、表示名) と、ドライバの設定 (ドライブ文字の自動割り当てなど) を書くだけで、
この環境に固有のパスや名前は入っていません。
