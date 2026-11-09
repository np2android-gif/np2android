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

android {
    namespace = "jp.sawada.np2android"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "jp.sawada.np2android"
        minSdk = 24
        targetSdk = 37
        versionCode = 20261109
        versionName = "0.86-a1"

        ndk {
            abiFilters += listOf("arm64-v8a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DANDROID_STL=c++_static", "-DNP2_IA32=ON")
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
