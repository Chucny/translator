// Phase 4d T7: real-ART JNI bridge diagnostics. Inputs come from tools/make_t7_bundle.sh.
plugins {
    id("com.android.application")
}
android {
    namespace = "com.chucny.translator.t7"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.chucny.translator.t7"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"
        ndk { abiFilters += "arm64-v8a" }
    }
    buildTypes {
        release { isMinifyEnabled = false }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    sourceSets.getByName("main") {
        java.srcDir(rootProject.file("../../../build/t7/java"))
        assets.srcDir(rootProject.file("../../../build/t7/assets"))
        jniLibs.srcDir(rootProject.file("../../../build/t7/jniLibs"))
    }
}
