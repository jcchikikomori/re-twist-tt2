// The Android app (docs/design/android-support.md). One module, `app`,
// whose native code is the repository's own CMakeLists.txt.
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

rootProject.name = "re-twist-tt2"
include(":app")
