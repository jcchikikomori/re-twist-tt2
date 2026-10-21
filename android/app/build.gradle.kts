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

// The tarballs CMake's FetchContent builds from: name -> (URL, SHA-256).
val sdlPins = listOf("SDL3", "SDL3_image", "SDL3_mixer").associateWith { name ->
    cmakeMatch("""FetchContent_Declare\($name\s+URL\s+(\S+)""") to
        cmakeMatch("""FetchContent_Declare\($name\s+URL\s+\S+\s+URL_HASH\s+SHA256=([0-9a-f]{64})""")
}

// Downloads a pinned tarball into build/sdl3/, unless a copy with the
// pinned SHA-256 is already there.
fun pinnedTarball(name: String): File {
    val (url, sha256) = sdlPins.getValue(name)
    val tarball = layout.buildDirectory.file("sdl3/${url.substringAfterLast('/')}").get().asFile
    fun hash(): String = MessageDigest.getInstance("SHA-256")
        .digest(tarball.readBytes()).joinToString("") { "%02x".format(it) }
    if (!tarball.isFile || hash() != sha256) {
        tarball.parentFile.mkdirs()
        URI(url).toURL().openStream().use { input ->
            tarball.outputStream().use { input.copyTo(it) }
        }
        val got = hash()
        if (got != sha256) {
            tarball.delete()
            throw GradleException("$url: SHA-256 $got, expected $sha256")
        }
    }
    return tarball
}

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
val sdlJavaDir = layout.buildDirectory.dir("generated/sdl3-java")

val fetchSdlJava by tasks.registering {
    inputs.property("pin", sdlPins.getValue("SDL3").toString())
    outputs.dir(sdlJavaDir)
    doLast {
        val tarball = pinnedTarball("SDL3")
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

// The game's data/ as APK assets (assets/data/). An allow-list, the same
// files CMake's install() ships: the source tree's data/ may also hold the
// player's own GameHouse common.tree and full.tree, which must never be
// packaged. The game copies them into its files folder on first launch
// (ui/include/tt2ui/bundled_data.hpp), guided by bundle-manifest.txt:
// "<sha256> <size> <path>" per file, which also stamps the copy.
val bundledAssetsDir = layout.buildDirectory.dir("generated/tt2-assets")

val bundleGameData by tasks.registering(Sync::class) {
    from(rootProject.file("../data")) {
        include("open-common.tree", "open-full.tree", "LICENSE.md", "original-assets.sha256")
        include("placeholder/**")
        into("data")
    }
    into(bundledAssetsDir)
    doLast {
        val data = bundledAssetsDir.get().dir("data").asFile
        val lines = data.walkTopDown().filter { it.isFile }.map { file ->
            val sha = MessageDigest.getInstance("SHA-256").digest(file.readBytes())
                .joinToString("") { "%02x".format(it) }
            "$sha ${file.length()} ${file.relativeTo(data).invariantSeparatorsPath}"
        }.sorted().toList()
        data.resolve("bundle-manifest.txt").writeText(lines.joinToString("\n", postfix = "\n"))
    }
}

// The notices of everything compiled into the APK, as assets/licenses/: the
// same files and names CMake's install() gives the AppImage and the zip (the
// fonts' and the dictionaries' licences travel inside data/). The SDL texts
// come from the pinned tarballs, so they always match the code that's built.
val licensesDir = layout.buildDirectory.dir("generated/tt2-licenses")
val repoLicenses = mapOf(
    "re-twist-tt2.txt" to rootProject.file("../LICENSE"),
    "super-xbr.txt" to rootProject.file("../third_party/super-xbr/LICENSE"),
    // Apache 2.0, which asks for the licence text with the binaries.
    "kotlin-stdlib.txt" to rootProject.file("../third_party/kotlin/LICENSE"),
)

val bundleLicenses by tasks.registering {
    inputs.property("pins", sdlPins.toString())
    inputs.files(repoLicenses.values)
    outputs.dir(licensesDir)
    doLast {
        project.delete(licensesDir)
        val out = licensesDir.get().dir("licenses").asFile
        out.mkdirs()
        repoLicenses.forEach { (name, file) -> file.copyTo(out.resolve(name)) }
        sdlPins.keys.forEach { name ->
            project.copy {
                from(project.tarTree(project.resources.gzip(pinnedTarball(name))))
                include("*/LICENSE.txt")
                eachFile { path = "$name.txt" }
                into(out)
            }
            if (!out.resolve("$name.txt").isFile) throw GradleException("$name: no LICENSE.txt in its tarball")
        }
    }
}

// D3: the release key. The keystore and its password stay on the user's
// machine and in GitHub secrets (packages.yml), never in the repository.
// Gradle reads TT2_KEYSTORE (a path), TT2_KEYSTORE_PASSWORD and
// TT2_KEY_ALIAS from ~/.gradle/gradle.properties or the environment; the
// key's password is the store's (a PKCS12 keystore has only one). Without
// them assembleRelease still builds, but unsigned, and nothing installs that.
fun signingValue(name: String): String? =
    providers.gradleProperty(name).orElse(providers.environmentVariable(name)).orNull?.takeIf { it.isNotEmpty() }

val releaseSigning = listOf("TT2_KEYSTORE", "TT2_KEYSTORE_PASSWORD", "TT2_KEY_ALIAS").associateWith(::signingValue)
if (releaseSigning.values.any { it != null } && releaseSigning.values.any { it == null }) {
    throw GradleException("release signing: set all of ${releaseSigning.keys} or none (missing: " +
        releaseSigning.filterValues { it == null }.keys + ")")
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

    signingConfigs {
        val keystore = releaseSigning["TT2_KEYSTORE"]
        val password = releaseSigning["TT2_KEYSTORE_PASSWORD"]
        val alias = releaseSigning["TT2_KEY_ALIAS"]
        if (keystore != null && password != null && alias != null) {
            create("release") {
                storeFile = file(keystore)
                storePassword = password
                keyAlias = alias
                keyPassword = password
            }
        }
    }

    buildTypes {
        getByName("release") {
            signingConfig = signingConfigs.findByName("release")
            // R8 stays off: JNI finds MainActivity's and ImportBridge's
            // methods by name, and the Kotlin is a small part of the APK.
            isMinifyEnabled = false
        }
        getByName("debug") {
            // Android only updates an app with an APK signed by the same key.
            // The suffix makes debug builds (each machine's own debug key) a
            // separate app, so they install next to a release APK instead of
            // over it. Separate saves and imported assets, too.
            // -PTT2_DEBUG_SAME_APP=true leaves the suffix off, so this build
            // updates a debug install made before the suffix existed (same
            // debug key), keeping its data.
            if (!providers.gradleProperty("TT2_DEBUG_SAME_APP").map(String::toBoolean).getOrElse(false)) {
                applicationIdSuffix = ".debug"
            }
        }
    }

    sourceSets["main"].java.srcDir(sdlJavaDir)
    sourceSets["main"].assets.srcDir(bundledAssetsDir)
    sourceSets["main"].assets.srcDir(licensesDir)

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

tasks.named("preBuild") { dependsOn(fetchSdlJava, bundleGameData, bundleLicenses) }

// JVM unit tests for the Kotlin importer (./gradlew testDebugUnitTest): no
// device, synthetic random bytes only, never real assets.
dependencies {
    testImplementation(libs.junit)
}
