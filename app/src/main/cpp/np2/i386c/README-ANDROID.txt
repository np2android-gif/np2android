i386c/ (IA-32 CPU コア)
=======================

xnp2 の i386c をそのまま使っている。Android 向けに変えたところを書く。
変更箇所には /* ANDROID-CPU */ の印を付けている (FPU の取り込みを始めたら /* ANDROID-FPU */ も使う)。

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
