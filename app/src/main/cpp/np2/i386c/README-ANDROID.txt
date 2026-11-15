i386c/ (IA-32 CPU コア)
=======================

xnp2 の i386c をそのまま使っている。Android 向けに変えたところを書く。
変更箇所には /* ANDROID-CPU */ の印を付けている。FPU の変更には /* ANDROID-FPU */ の印を付けている (下の「FPU」)。

ia32_panic でアプリを終了させない (IA32_REBOOT_ON_PANIC)
  ia32_panic はトリプルフォルトや未実装の命令など、CPU が先に進めないときに呼ばれる。xnp2 の既定は
  exit(1) でアプリごと終了する。実機ではトリプルフォルトで CPU がリセットされるので、
  CMakeLists.txt の NP2_CPU_DEFINES に IA32_REBOOT_ON_PANIC を足し、エミュレータをリセットするようにした
  (np2kai と同じ定義)。
  ただし xnp2 の IA32_REBOOT_ON_PANIC の処理 (pccore_reset() を呼んで siglongjmp) は、そのままでは使えない。
  pccore_reset() の nevent_allreset() がフレームのイベント (NEVENT_FLAMES) まで消し、pccore_exec の
  ループが終わらなくなるため。ia32/interface.c で次のように置き換えた:
    - CPU_RESETREQ を立てる (pccore_exec の次の周で CPU_SHUT()、CPU だけのリセット)
    - pcstat.hardwarereset を立てる (フレームの終わりで pccore_reset()、メニューのリセットと同じ)
    - port/np2bridge.c の np2bridge_cpureset() で Kotlin に通知し、トースト
      「CPU が停止したためリセットしました」を出す。レジスタは ia32_panic の msgbox (SDL_Log) で
      logcat に出る
    - siglongjmp(exec_1step_jmpbuf, 2) で ia32() / ia32_step() に戻る
  siglongjmp で戻れるのは ia32() / ia32_step() の sigsetjmp の内側を実行している間だけなので、
  ia32_in_exec で見分ける。外側 (pccore_exec の pic_irq() からの割り込みの受け付けなど) で
  ia32_panic になったときは、今までどおり終了する。
  注意 (未対応): pic_irq() からの割り込みで例外 (EXCEPTION) が起きると、exception() は
  siglongjmp(exec_1step_jmpbuf, 1) で、すでに戻った ia32() の jmpbuf に飛ぶ。xnp2 から変わらない動きで、
  今回は手を付けていない。
  確かめたこと (2026-11-12、XZ3): IDT のリミットを 0 にして INT 3 を実行する .COM でトリプルフォルトを
  起こし、アプリが終了せずにリセットしてトーストが出ること。MS-DOS 6.2・FreeDOS(98) の起動、
  a8 で保存したステートのロードが変わらないこと。

FPU (SoftFloat 3e 版、CMake の NP2_FPU、/* ANDROID-FPU */)
  取り込み元: NP2kai (https://github.com/AZO234/NP2kai、wx_alpha 5939e0c) の i386c/ia32/instructions/fpu/。
  ライセンス: すべて BSD (GPL のものは無い)。アプリの「このアプリについて」の fpu.txt・softfloat3.txt に
  著作権表示を載せている (app/src/main/assets/licenses/)。
    instructions/fpu/fpemul_softfloat3.cpp  BSD 2 条項 (Copyright (c) 2012 NONAKA Kimihiro、modified by SimK)
    instructions/fpu/fpumem.h               BSD 2 条項 (Copyright (c) 2018 SimK)
    instructions/fpu.c の SoftFloat 版の部分 BSD 2 条項 (Copyright (c) 2012 NONAKA Kimihiro。NP2kai の fpu.c から)
    instructions/fpu/softfloat3/            BSD 3 条項 (Berkeley SoftFloat 3e、The Regents of the University of
                                            California)。COPYING.txt をそのまま残している
  softfloat3/ の .c はリンクに要る 45 個だけにした (NP2kai の 304 個のうち)。MSVC 用の include/stdbool.h と
  opts-MSVC.h (ライセンスの文が無く、_MSC_VER のときだけ読まれる) は入れていない。DOSBox 版の FPU (GPL) も入れていない。

  ビルド: CMakeLists.txt の option NP2_FPU (既定 OFF)。app/build.gradle.kts で ON にしている。OFF にすると
  今までどおり fpdummy.c だけの 486SX になる (FPU が原因の不具合を切り分けるときに使う)。
  CPUID・CR0 は xnp2 の cpu.h・system_inst.c のまま USE_FPU で変わる (486DX、ファミリー 4・モデル 1、EDX bit0。
  CR0 は ET=1・EM=0)。

  取り込んだファイルから変えたところ:
    fpemul_softfloat3.cpp
      - FLDL2T・FLDL2E・FLDPI・FLDLG2・FLDLN2 は、80 ビットの値 (Intel の資料と同じ最近接偶数) を直接入れる。
        NP2kai は double の定数から変換するので、仮数の下 11 ビットが 0 になっていた (FLDPI は …C000、
        実機は …C235)。丸めのモード (RC) で最後のビットが変わる実機の動きは真似ていない。
      - ia32_panic 2 か所を #UD にした (ESC1 の default と SF_FPU_FXSAVERSTOR の default)。どちらも xnp2 からは
        来ない (ESC1 の idx は 0〜7 がすべて case にあり、SF_FPU_FXSAVERSTOR は呼ばれない)。今は届かないが、
        先々 NP2kai から命令表などを取り込んで届くようになったときの備えとして #UD にしている。
  xnp2 の側で変えたところ:
    ia32/cpu.h      FPU_REGS_S・FPU_STAT_S (NP2kai と同じ配置。SSE・MMX・DOSBox 版の項目は除く)。
                    FPU の状態は I386STAT に入れず、別の i386fpu に置く (下の「ステート」)。
    ia32/ia32.c     i386fpu の実体
    ia32/ia32.mcr   GET_MODRM_PCBYTE (fpemul_softfloat3.cpp が使う)
    instructions/fpu.c  NP2kai の fpu.c から SoftFloat 版で使う部分 (命令表の差し替え、ステートのロード後の
                    丸めとフラグ、メモリの読み書き)。FWAIT は Intel の仕様どおり MP と TS が両方立っているとき
                    だけ #NM
    ia32/interface.c  ia32_initreg で fpu_initialize(1) (命令表を SoftFloat 版にし、FNINIT 相当)
    statsave.c・statsave.tbl  下の「ステート」
  ステート:
    FPU の状態はステートの "FPU" の項目に入れる (statsave.tbl)。"CPU286" の大きさは FPU 無しの版と同じなので、
    a6〜a12 のレジュームとスロットはそのまま読める。"FPU" が無いファイルを読んだときは、ロードの前の CPU_RESET で
    FNINIT 相当になったまま動く。statsave_load の最後 (全部の項目を読んだ後) で fpu_statesave_load() が
    SoftFloat の丸めとフラグを FPU の状態に合わせる。
    "FPU" の入ったステートを FPU 無しの版 (a12 以前) で読むと、知らない項目なので statsave_check が
    STATFLAG_WARNING を返し、アプリは読まずに断る。
    注意: FPU 無しの版で保存した、Windows 2000 などが動いている最中のステートを読むと、ゲストは
    「FPU が無い」と決めたまま動く。読んだ後は一度ゲストを再起動するのがよい (リリースノートに書く)。
  記録だけ (直していない):
    - CR0.NE=0 のときも、FPU のエラーは常に #MF になる (PC-98 の割り込みにはつながない)。既定の制御ワードでは
      起きない。
    - FPU 有りでは MOV CR0 で EM が常に 0 になる (xnp2 の元のまま)。
    - CPUID の CMOV のビットが立っている (xnp2 の元のまま。486 には無い命令)。
