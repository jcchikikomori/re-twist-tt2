package com.jcchikikomori.retwisttt2.importer

// The original-asset manifest (data/original-assets.sha256), read with the
// same rules as engine/include/tt2/asset_manifest.hpp, so the Kotlin
// importer accepts exactly the manifests the C++ game accepts:
//
//   # re-twist-tt2 original-asset manifest
//   # version=1
//   <64 lowercase hex>  images/gui/button_ok.png
//
// Every rule decides from the path alone, before any file is opened.
object AssetManifest {
    const val TITLE = "# re-twist-tt2 original-asset manifest"
    const val VERSION = "# version=1"

    // tt2::IMPORT_ROOTS: the only folders an entry may sit in.
    val IMPORT_ROOTS = listOf(
        "images/splash", "images/loadscene", "images/selectionscene", "images/gui", "images/gamescene",
        "images/trophyscene", "images/blitzscene", "fonts", "music", "sound",
    )

    // tt2::INSTALL_PROBE: "is this an install?"
    const val INSTALL_PROBE = "properties/resources.xml"

    data class Entry(val path: String, val sha256: String)   // sha256: 64 lowercase hex

    // ASCII only, like the C++ lower(): no locale surprises (Turkish i).
    fun lower(s: String): String = buildString(s.length) {
        for (c in s) append(if (c in 'A'..'Z') c + ('a' - 'A') else c)
    }

    // Never imported: code, dictionaries, DRM / licensing files, and the
    // caches Windows and macOS leave in folders.
    fun isExcluded(path: String): Boolean {
        val name = lower(path.substringAfterLast('/'))
        if (name == "texttwist2_radrm.dll" || name == "thumbs.db" || name == "desktop.ini" || name == ".ds_store")
            return true
        return listOf(".nfo", ".dll", ".exe", ".tree").any { name.endsWith(it) }
    }

    // Forward slashes, stays inside its folder: no leading or trailing '/',
    // no ':' or '\', no empty, "." or ".." segment, no control characters.
    fun isSafeRelative(p: String): Boolean {
        if (p.isEmpty() || p.first() == '/' || p.last() == '/') return false
        if (p.any { it == '\\' || it == ':' || it < ' ' }) return false
        return p.split('/').none { it.isEmpty() || it == "." || it == ".." }
    }

    fun underImportRoot(p: String): Boolean =
        IMPORT_ROOTS.any { p.length > it.length + 1 && p.startsWith(it) && p[it.length] == '/' }

    private fun isDigest(s: String) = s.length == 64 && s.all { it in '0'..'9' || it in 'a'..'f' }

    // All or nothing: null on any bad line. The title must be the first
    // line, the version comment must come before the first entry, and paths
    // are unique ignoring case.
    fun parse(text: String): List<Entry>? {
        val out = mutableListOf<Entry>()
        val seen = HashSet<String>()
        var versionSeen = false
        val lines = text.split('\n').let { if (text.endsWith('\n')) it.dropLast(1) else it }
        if (text.isEmpty() || lines.isEmpty()) return null
        for ((i, raw) in lines.withIndex()) {
            val line = raw.removeSuffix("\r")
            if (i == 0) {
                if (line != TITLE) return null
                continue
            }
            if (line.isEmpty()) continue
            if (line.first() == '#') {
                if (line == VERSION) versionSeen = true
                else if (line.startsWith("# version=")) return null   // a version this build doesn't know
                continue
            }
            if (!versionSeen) return null
            if (line.length < 67 || line.substring(64, 66) != "  ") return null
            val digest = line.substring(0, 64)
            val path = line.substring(66)
            if (!isDigest(digest)) return null
            if (!isSafeRelative(path) || !underImportRoot(path) || isExcluded(path)) return null
            if (!seen.add(lower(path))) return null
            out += Entry(path, digest)
        }
        return if (versionSeen) out else null
    }
}
