import java.net.URI
import java.security.MessageDigest

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

// The repository's CMakeLists.txt is the one place that pins the game's
// version and the SDL3 release, so both are read from it instead of being
// copied here (a copy would drift on the next bump).
val rootCMake = rootProject.file("../CMakeLists.txt")
val rootCMakeText = rootCMake.readText()

fun cmakeMatch(pattern: String): String =
    Regex(pattern).find(rootCMakeText)?.groupValues?.get(1)
        ?: throw GradleException("CMakeLists.txt: no match for /$pattern/")

val gameVersion = cmakeMatch("""project\(tt2\s+VERSION\s+(\d+\.\d+\.\d+)""")
val sdlUrl = cmakeMatch("""FetchContent_Declare\(SDL3\s+URL\s+(\S+)""")
val sdlSha256 = cmakeMatch("""FetchContent_Declare\(SDL3\s+URL\s+\S+\s+URL_HASH\s+SHA256=([0-9a-f]{64})""")

// D10 in the plan: versionCode comes from SemVer, MAJOR*10000 + MINOR*100 + PATCH
// (0.6.0 -> 600), so every release installs over the previous one.
val gameVersionCode = gameVersion.split('.').map(String::toInt).let { (major, minor, patch) ->
    major * 10000 + minor * 100 + patch
}

// SDLActivity and its helpers are Java sources inside the SDL3 tarball, and
// they must match the libSDL3.so that CMake builds from the same tarball, or
// the app dies at startup with "JNI method not found". Gradle compiles Java
// before CMake's FetchContent runs, so this task fetches the tarball itself,
// checks the pinned SHA-256 and extracts only org/libsdl/app/.
val sdlTarball = layout.buildDirectory.file("sdl3/${sdlUrl.substringAfterLast('/')}")
val sdlJavaDir = layout.buildDirectory.dir("generated/sdl3-java")

val fetchSdlJava by tasks.registering {
    inputs.property("url", sdlUrl)
    inputs.property("sha256", sdlSha256)
    outputs.dir(sdlJavaDir)
    doLast {
        val tarball = sdlTarball.get().asFile
        fun sha256(): String = MessageDigest.getInstance("SHA-256")
            .digest(tarball.readBytes()).joinToString("") { "%02x".format(it) }
        if (!tarball.isFile || sha256() != sdlSha256) {
            tarball.parentFile.mkdirs()
            URI(sdlUrl).toURL().openStream().use { input ->
                tarball.outputStream().use { input.copyTo(it) }
            }
            val got = sha256()
            if (got != sdlSha256) {
                tarball.delete()
                throw GradleException("$sdlUrl: SHA-256 $got, expected $sdlSha256")
            }
        }
        val out = sdlJavaDir.get().asFile
        project.delete(out)
        project.copy {
            from(project.tarTree(project.resources.gzip(tarball)))
            include("*/android-project/app/src/main/java/org/libsdl/app/**")
            eachFile { path = path.substringAfter("/app/src/main/java/") }
            includeEmptyDirs = false
            into(out)
        }
    }
}

// The game's data/ as APK assets (assets/data/), read through SDL's file
// API. An allow-list, the same files CMake's install() ships: the source
// tree's data/ may also hold the player's own GameHouse common.tree and
// full.tree, which must never be packaged.
val bundledAssetsDir = layout.buildDirectory.dir("generated/tt2-assets")

val bundleGameData by tasks.registering(Sync::class) {
    from(rootProject.file("../data")) {
        include("open-common.tree", "open-full.tree", "LICENSE.md", "original-assets.sha256")
        include("placeholder/**")
        into("data")
    }
    into(bundledAssetsDir)
}

android {
    namespace = "com.jcchikikomori.retwisttt2"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.jcchikikomori.retwisttt2"
        minSdk = 31   // Android 12; the Legacy Edition (5-11) is a separate plan
        targetSdk = 35
        versionCode = gameVersionCode
        versionName = gameVersion

        ndk {
            abiFilters += "arm64-v8a"   // D6: real-device testing only
        }
        externalNativeBuild {
            cmake {
                // CMakeLists.txt skips the desktop-only targets when ANDROID is set.
                arguments += listOf(
                    "-DTT2_FETCH_SDL3=ON",
                    "-DTT2_BUILD_TESTS=OFF",
                    "-DTT2_WARN_AS_ERROR=ON",
                    "-DANDROID_STL=c++_static",
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = rootCMake
            version = "3.22.1"
        }
    }

    sourceSets["main"].java.srcDir(sdlJavaDir)
    sourceSets["main"].assets.srcDir(bundledAssetsDir)

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        buildConfig = false
    }
}

kotlin {
    jvmToolchain(17)
}

tasks.named("preBuild") { dependsOn(fetchSdlJava, bundleGameData) }
