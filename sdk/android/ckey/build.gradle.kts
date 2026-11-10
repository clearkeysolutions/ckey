// org.openckey:ckey — the headless core AAR: Kotlin API + libckey_jni.so.
// No library dependencies (only the Kotlin standard library).
plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    `maven-publish`
}

description = "CKey optical code for Android: encoding, native rendering, scanning and opt-out aggregate telemetry."

fun flag(name: String): String =
    if ((findProperty(name) as String? ?: "true").toBoolean()) "ON" else "OFF"

android {
    namespace = "org.openckey.ckey"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        minSdk = 24
        consumerProguardFiles("consumer-rules.pro")
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DCKEY_ANDROID_SCAN=${flag("ckey.native.scan")}",
                    "-DCKEY_ANDROID_RENDER=${flag("ckey.native.render")}",
                    "-DCKEY_ANDROID_TELEMETRY=${flag("ckey.native.telemetry")}",
                )
            }
        }
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64", "x86")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    testOptions {
        // Unit tests are pure JVM (payload rules, telemetry validation, sender);
        // they never load the native library.
        unitTests.isReturnDefaultValues = true
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.test.runner)
    androidTestImplementation(libs.androidx.test.ext.junit)
}
