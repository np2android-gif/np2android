import java.security.MessageDigest
import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
}

/**
 * リリース署名の設定。keystore.properties (git 管理外) が無い環境では
 * リリース版は署名なしでビルドされる (デバッグ版には影響しない)。
 */
val keystoreProps = Properties().apply {
    val f = rootProject.file("keystore.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}

/**
 * font16.bin を作り直したのに FontGlyphs.VERSION を上げ忘れると、もう作った font.tmp が作り直されない。
 * 今の font16.bin の SHA-256 が、tools/font16/versions.txt の今の VERSION の行と合うかを確かめる。
 */
val checkFont16 = tasks.register("checkFont16") {
    val bin = file("src/main/assets/font16.bin")
    val kt = file("src/main/kotlin/jp/sawada/np2android/FontGlyphs.kt")
    val table = rootProject.file("tools/font16/versions.txt")
    inputs.files(bin, kt, table)
    doLast {
        val version = Regex("""const val VERSION = (\d+)""").find(kt.readText())?.groupValues?.get(1)
            ?: throw GradleException("FontGlyphs.kt に VERSION が見つかりません")
        val sha = MessageDigest.getInstance("SHA-256").digest(bin.readBytes())
            .joinToString("") { "%02x".format(it) }
        val rows = table.readLines().map { it.trim() }.filter { it.isNotEmpty() && !it.startsWith("#") }
            .map { it.split(Regex("""\s+""")) }
        val want = rows.firstOrNull { it[0] == version }?.get(1)
        if (want != sha) {
            val other = rows.firstOrNull { it[1] == sha }?.get(0)
            throw GradleException(
                "font16.bin (SHA-256 $sha) が FontGlyphs.VERSION = $version と合いません" +
                    (if (other != null) " (この font16.bin は版 $other のもの)" else "") +
                    "。font16.bin を変えたら VERSION を上げ、tools/font16/versions.txt に行を足してください"
            )
        }
    }
}
tasks.named("preBuild") { dependsOn(checkFont16) }

android {
    namespace = "jp.sawada.np2android"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "jp.sawada.np2android"
        minSdk = 24
        targetSdk = 37
        versionCode = 20261132
        versionName = "0.86-a24"

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DNP2_IA32=ON", "-DNP2_FPU=ON")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }

    signingConfigs {
        if (keystoreProps.getProperty("storeFile") != null) {
            create("release") {
                storeFile = file(keystoreProps.getProperty("storeFile"))
                storePassword = keystoreProps.getProperty("storePassword")
                keyAlias = keystoreProps.getProperty("keyAlias")
                keyPassword = keystoreProps.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            // R8 で未使用コードの削除・最適化・難読化を行う (JNI で使うものは proguard-rules.pro で保護)
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.findByName("release")
        }
        // 性能測定用: リリースと同じ最適化 (C は -O2) でデバッグ署名。デバッグ版に上書きでき、端末のデータが残る。
        // src/profile/AndroidManifest.xml で profileable (simpleperf で計測可能) にしている。
        create("profile") {
            initWith(getByName("release"))
            signingConfig = signingConfigs.getByName("debug")
            matchingFallbacks += listOf("release")
        }
    }
    buildFeatures {
        // デバッグ用ログを BuildConfig.DEBUG で切り替える
        buildConfig = true
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    lint {
        abortOnError = false
    }
}
