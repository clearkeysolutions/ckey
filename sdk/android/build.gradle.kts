// Root build: shared publication settings for every module.
plugins {
    alias(libs.plugins.android.library) apply false
    alias(libs.plugins.kotlin.android) apply false
    alias(libs.plugins.kotlin.compose) apply false
}

/** The SDK version is the repository's VERSION file (e.g. 0.1.0-build.65). */
val ckeyVersion: String = rootDir.resolve("../../VERSION").readText().trim()
require(Regex("^0\\.[0-9]+\\.[0-9]+(-(build|dev)\\.[0-9]{1,6})?$").matches(ckeyVersion)) {
    "VERSION '$ckeyVersion' does not match the CKey version scheme"
}

subprojects {
    group = "org.openckey"
    version = ckeyVersion

    plugins.withId("maven-publish") {
        afterEvaluate {
            extensions.configure<PublishingExtension> {
                publications {
                    create<MavenPublication>("release") {
                        from(components["release"])
                        groupId = "org.openckey"
                        artifactId = project.name
                        version = ckeyVersion
                        pom {
                            name.set(project.name)
                            description.set(project.description)
                            url.set("https://github.com/clearkeysolutions/ckey")
                            licenses {
                                license {
                                    name.set("MIT License")
                                    url.set("https://opensource.org/licenses/MIT")
                                    distribution.set("repo")
                                }
                            }
                            developers {
                                developer {
                                    id.set("clearkeysolutions")
                                    name.set("ClearKey Solutions")
                                    url.set("https://github.com/clearkeysolutions")
                                }
                            }
                            scm {
                                url.set("https://github.com/clearkeysolutions/ckey")
                                connection.set("scm:git:https://github.com/clearkeysolutions/ckey.git")
                                developerConnection.set("scm:git:ssh://git@github.com/clearkeysolutions/ckey.git")
                            }
                        }
                    }
                }
                // No credentials here. CI publishes to a local directory; release
                // automation adds its own repository (and signing) from secrets.
                repositories {
                    maven {
                        name = "local"
                        url = uri(rootProject.layout.buildDirectory.dir("maven"))
                    }
                }
            }
        }
    }
}
