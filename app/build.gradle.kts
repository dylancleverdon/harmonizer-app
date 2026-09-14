plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

// Every build needs a versionCode strictly greater than the installed one or
// Android refuses the update. GitHub's run number is monotonic per workflow;
// the offset keeps it clear of the hardcoded 1 that early builds shipped with.
val ciRunNumber = providers.environmentVariable("GITHUB_RUN_NUMBER").orNull?.toIntOrNull()
val appVersionCode = 100 + (ciRunNumber ?: 0)
val appVersionName = "1.0.${ciRunNumber ?: 0}"

// The app fetches these to find out whether a newer build exists. They are
// permalinks to whatever the newest release holds, so they never need updating.
val updateBase = "https://github.com/dylancleverdon/harmonizer-app/releases/latest/download"

android {
    namespace = "com.dylan.harmonizer"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.dylan.harmonizer"
        // AAudio's low-latency path, the MIDI manager and AudioDeviceInfo all
        // predate this; 29 simply avoids carrying compatibility code the S23
        // Ultra will never execute.
        minSdk = 29
        targetSdk = 35
        versionCode = appVersionCode
        versionName = appVersionName

        buildConfigField("String", "UPDATE_MANIFEST_URL", "\"$updateBase/version.json\"")
        buildConfigField("String", "UPDATE_APK_URL", "\"$updateBase/harmonizer.apk\"")

        ndk {
            // The S23 Ultra is arm64 only. Building a single ABI keeps the APK
            // small and the CI build fast.
            abiFilters += listOf("arm64-v8a")
        }

        externalNativeBuild {
            cmake {
                // Oboe's prefab package requires the shared STL.
                arguments += listOf("-DANDROID_STL=c++_shared")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildFeatures {
        compose = true
        // Oboe ships its headers and .so through prefab.
        prefab = true
        // Carries the update URLs through to Kotlin.
        buildConfig = true
    }

    signingConfigs {
        create("release") {
            // Committed to the repository on purpose; see keystore/README.md for
            // what that costs and how to rotate away from it. The point is that
            // the signature stays identical across builds, which is the only way
            // Android will install one over another.
            storeFile = rootProject.file("keystore/release.jks")
            storePassword = "harmonizer"
            keyAlias = "harmonizer"
            keyPassword = "harmonizer"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = signingConfigs.getByName("release")
        }
        debug {
            isJniDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")

    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")

    implementation("com.google.oboe:oboe:1.9.0")
}
