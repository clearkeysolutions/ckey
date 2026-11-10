// org.openckey:ckey-camerax — CameraX ImageAnalysis.Analyzer (CKeyAnalyzer). Optional.
plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    `maven-publish`
}

description = "CameraX analyzer for the CKey Android SDK."

android {
    namespace = "org.openckey.ckey.camerax"
    compileSdk = 34
    defaultConfig { minSdk = 24 }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    publishing { singleVariant("release") { withSourcesJar() } }
}

kotlin {
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
}

dependencies {
    api(project(":ckey"))
    // compileOnly would force apps to add CameraX themselves; they need it anyway to bind a camera.
    api(libs.camerax.core)
}
