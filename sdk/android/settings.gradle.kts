// CKey Android SDK (Maven: org.openckey:ckey, org.openckey:ckey-compose, org.openckey:ckey-camerax).
// Build from this directory: `gradle -p sdk/android build` (Gradle 8.10.2+, see gradle/wrapper).
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "ckey-android"
include(":ckey", ":ckey-compose", ":ckey-camerax")
