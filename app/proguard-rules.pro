# リリースビルド (R8 による最適化・難読化) で消されたり名前を変えられたりすると困るもの。
# どちらも C 側 (libSDL2.so / libmain.so) から JNI で名前を指定して呼ばれる。

# SDL2 の Java 側。C から多数のメソッドが名前で呼ばれるので、まとめて保持する
# (SDL2 テンプレートの proguard-rules.pro は個別指定だが、取りこぼしを避けるため丸ごと)
-keep class org.libsdl.app.** { *; }

# エミュレータとの橋渡し。external 関数 (C で実装) と、C から呼ばれる onNativeEvent
-keep class jp.sawada.np2android.Np2Native {
    *;
}

# Activity は SDL が getLibraries() 等をオーバーライド経由で呼ぶ (通常の keep で残るが念のため)
-keep class jp.sawada.np2android.Np2Activity { *; }

# ネイティブメソッドを持つクラスは名前を保持
-keepclasseswithmembernames class * {
    native <methods>;
}
