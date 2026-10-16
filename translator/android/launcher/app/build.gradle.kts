// Chucny's Translator launcher. Generated native/runtime inputs come only from build/launcher.
// Plain Java, UI built in code, no AndroidX.
import java.io.File
import java.util.Properties

plugins {
    id("com.android.application")
}

// The release key lives outside the repository: ~/.chucnys-translator/signing.properties names the
// keystore and its passwords. Without that file the release build stays unsigned, so a clone
// without the key still builds.
val signingProps = Properties()
val signingPropsFile = File(System.getProperty("user.home"), ".chucnys-translator/signing.properties")
if (signingPropsFile.isFile) {
    signingPropsFile.inputStream().use { stream -> signingProps.load(stream) }
}
val releaseKeystore: File? = signingProps.getProperty("storeFile")?.let { path -> File(path) }
val hasReleaseKey = releaseKeystore != null && releaseKeystore.isFile

android {
    namespace = "com.chucny.translator.launcher"
    compileSdk = 35

    defaultConfig {
        // The installed app identity is stable for imports, diagnostics and app-private storage.
        applicationId = "com.chucny.translator.bridge"
        minSdk = 26
        // targetSdk is deliberately not the newest, and not lower either: a plugin's Java code runs
        // in this process, and the framework keys its compatibility changes on the *process*
        // targetSdk, not on the plugin's own.
        //
        // Above 30 breaks 2016 code. com.nianticlabs.nia.location.LocationManagerProvider calls
        // LocationManager.addGpsStatusListener, which throws UnsupportedOperationException under
        // compat change BLOCK_GPS_STATUS_USAGE (144027538, "enableSinceTargetSdk=31", platform
        // message: "threshold (30)") on any modern device, killing the :guest process from a
        // ContextService handler thread. The same key decides PendingIntent mutability flags,
        // runtime-receiver export flags and foreground service types, all of which plugin code
        // uses in the pre-S form. The platform also refuses to override such a change for an app
        // whose own targetSdk is above the threshold, so this is the only lever.
        //
        // Below 28 breaks the launcher: Android 16's ro.build.version.min_supported_target_sdk is
        // 28, and a lower targetSdk makes the framework put a modal "This app was built for an
        // older version of Android" dialog in front of every activity launch. That dialog blocks
        // the :guest process's first window, which then never finishes its Unity start-up.
        // 28 is therefore the lowest value that works, and the oldest era a modern device allows.
        targetSdk = 28
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += "arm64-v8a" }
    }

    signingConfigs {
        if (hasReleaseKey) {
            create("release") {
                storeFile = releaseKeystore
                storePassword = signingProps.getProperty("storePassword")
                keyAlias = signingProps.getProperty("keyAlias")
                keyPassword = signingProps.getProperty("keyPassword")
            }
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            if (hasReleaseKey) signingConfig = signingConfigs.getByName("release")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    sourceSets.getByName("main") {
        assets.srcDir(rootProject.file("../../build/launcher/assets"))
        jniLibs.srcDir(rootProject.file("../../build/launcher/jniLibs"))
        // This public source set contains the generic launcher only; apps are imported by users.
        assets.srcDir("src/main/assets")
    }
}

dependencies {
    // Apache-2.0. Lifts hidden API restrictions for the framework hooks in the :guest process.
    implementation("org.lsposed.hiddenapibypass:hiddenapibypass:6.1")
}
