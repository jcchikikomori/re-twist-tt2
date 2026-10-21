package com.jcchikikomori.retwisttt2.importer

import java.io.File
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipFile

// tt2ui::ImportError::Kind, by name: the names cross JNI, and C++ turns
// them into the failure text (import_prompt.hpp). Keep the two lists equal.
enum class ImportErrorKind { None, NotAnInstall, EmptyManifest, Missing, Mismatch, WriteFailed, CommitFailed, BadArchive }

sealed interface ImportResult {
    data class Imported(val files: Int) : ImportResult
    // `paths`: the first ZipImporter.ERROR_PATHS problem files; `count`: all of them.
    data class Failed(val kind: ImportErrorKind, val count: Int = 0, val paths: List<String> = emptyList()) : ImportResult
    data object Cancelled : ImportResult
}

// Imports the original art, fonts and sounds from a ZIP of the player's own
// Text Twist 2 folder: the Android twin of tt2ui::AssetImportJob, with the
// same three passes and the same folder swap.
//
//   1. verify   hash every manifest entry inside the ZIP; any missing or
//               different file fails the import and writes nothing
//   2. copy     extract those entries into <assetsDir>.staging
//   3. verify   hash the copies again (disk trouble)
//   4. commit   write the stamp last, <dir> -> <dir>.old,
//               <dir>.staging -> <dir>, drop .old
//
// Files are written to the paths the manifest names, never to the paths
// inside the ZIP, so no entry can land outside the staging folder; an
// archive holding an unsafe name (absolute, "..", a drive letter) is
// rejected outright anyway. Only manifest entries are ever opened, and the
// manifest can't list DRM, code or dictionary files.
//
// `stampText` is tt2ui::serialize_stamp() output, made by C++ (T3.3); it is
// written byte for byte, and C++ import_status() reads it on every launch.
class ZipImporter(
    private val manifest: List<AssetManifest.Entry>,
    private val stampText: String,
    assetsDir: File,
    private val maxEntryBytes: Long = MAX_ENTRY_BYTES,
    private val maxTotalBytes: Long = MAX_TOTAL_BYTES,
) {
    companion object {
        const val IMPORT_STAMP_FILE = "import-stamp.txt"   // tt2ui::IMPORT_STAMP_FILE
        const val ERROR_PATHS = 5                          // tt2ui::IMPORT_ERROR_PATHS

        // The real install is 323 files, 19.3 MB, the largest 1.9 MB. The
        // caps leave room for other versions and stop a ZIP bomb early.
        const val MAX_ENTRY_BYTES = 16L shl 20
        const val MAX_TOTAL_BYTES = 256L shl 20
        const val MAX_ZIP_BYTES = 1L shl 30   // the picked file, copied to the cache

        private const val CHUNK = 64 * 1024

        fun stagingDir(dir: File) = File(dir.parentFile, dir.name + ".staging")
        fun oldDir(dir: File) = File(dir.parentFile, dir.name + ".old")

        // An entry name with '\' read as '/' (some Windows tools write them).
        // Directories end in '/', which is fine; the rest must stay relative.
        fun isSafeArchiveName(raw: String): Boolean {
            val name = raw.replace('\\', '/').removeSuffix("/")
            if (name.isEmpty() || name.first() == '/' || name.any { it == ':' || it < ' ' }) return false
            return name.split('/').none { it == ".." }
        }

        // ZipFile needs a real file, so the picked content:// stream is copied
        // into the cache first. False (and nothing left behind) past `maxBytes`.
        fun copyToCache(input: InputStream, to: File, maxBytes: Long = MAX_ZIP_BYTES,
                        cancelled: () -> Boolean = { false }): Boolean {
            to.parentFile?.mkdirs()
            var ok = false
            try {
                to.outputStream().use { out ->
                    val buf = ByteArray(CHUNK)
                    var total = 0L
                    while (true) {
                        if (cancelled()) return false
                        val n = input.read(buf)
                        if (n < 0) break
                        total += n
                        if (total > maxBytes) return false
                        out.write(buf, 0, n)
                    }
                }
                ok = true
            } catch (e: IOException) {
                return false
            } finally {
                if (!ok) to.delete()
            }
            return true
        }
    }

    private val dir = assetsDir.absoluteFile
    private val staging = stagingDir(dir)
    private val old = oldDir(dir)

    private class Cancel : RuntimeException()
    private class TooBig : RuntimeException()

    private var doneBytes = 0L
    private var totalBytes = 0L

    // Blocking; run it off the main thread. `progress` gets 0..1 across the
    // three passes. `onOpen` sees every ZIP entry name that is read (tests).
    fun run(zip: File, progress: (Float) -> Unit = {}, cancelled: () -> Boolean = { false },
            onOpen: (String) -> Unit = {}): ImportResult {
        doneBytes = 0
        totalBytes = 0
        // Android 14+ already refuses ".." and leading '/' names here (apps
        // targeting API 34); isSafeArchiveName covers the JVM tests and API 31-33.
        val zf = try {
            ZipFile(zip)
        } catch (e: IOException) {
            return ImportResult.Failed(ImportErrorKind.BadArchive)   // not a ZIP, or a damaged one
        }
        return try {
            zf.use { importFrom(it, progress, cancelled, onOpen) }
        } catch (e: Cancel) {
            staging.deleteRecursively()
            ImportResult.Cancelled
        } catch (e: IOException) {
            staging.deleteRecursively()
            ImportResult.Failed(ImportErrorKind.BadArchive)   // the ZIP broke while reading it
        }
    }

    private fun importFrom(zf: ZipFile, progress: (Float) -> Unit, cancelled: () -> Boolean,
                           onOpen: (String) -> Unit): ImportResult {
        val files = mutableListOf<Pair<String, ZipEntry>>()
        for (e in zf.entries()) {
            if (!isSafeArchiveName(e.name)) return ImportResult.Failed(ImportErrorKind.BadArchive)
            if (!e.isDirectory) files += e.name.replace('\\', '/') to e
        }

        // A ZIP of the folder has a "Text Twist 2/" prefix, a ZIP of its
        // contents has none: the install root is wherever the probe sits.
        val probe = AssetManifest.INSTALL_PROBE
        val root = files.map { it.first }
            .filter { AssetManifest.lower(it).let { n -> n == probe || n.endsWith("/$probe") } }
            .minByOrNull { it.length } ?: return ImportResult.Failed(ImportErrorKind.NotAnInstall)
        val prefix = root.dropLast(probe.length)
        if (manifest.isEmpty()) return ImportResult.Failed(ImportErrorKind.EmptyManifest)

        // Manifest path (lower case) -> its entry. Matching ignores case: a
        // Windows folder does, and the manifest's paths are unique that way.
        val wanted = manifest.mapTo(HashSet()) { AssetManifest.lower(it.path) }
        val byPath = HashMap<String, ZipEntry>()
        for ((name, e) in files) {
            if (!name.startsWith(prefix, ignoreCase = true)) continue
            val key = AssetManifest.lower(name.substring(prefix.length))
            if (key !in wanted) continue
            if (byPath.put(key, e) != null) return ImportResult.Failed(ImportErrorKind.BadArchive)   // two candidates
        }
        totalBytes = manifest.sumOf { m -> byPath[AssetManifest.lower(m.path)]?.size?.coerceAtLeast(0) ?: 0 }
        if (totalBytes > maxTotalBytes) return ImportResult.Failed(ImportErrorKind.BadArchive)
        progress(0f)

        // 1. Verify inside the ZIP.
        val problems = mutableListOf<String>()
        var mismatch = false
        for (m in manifest) {
            val e = byPath[AssetManifest.lower(m.path)]
            if (e == null) {
                problems += m.path
                continue
            }
            onOpen(e.name)
            val got = if (e.size > maxEntryBytes) null else hash(zf.getInputStream(e), null, progress, cancelled)
            if (got != m.sha256) {
                mismatch = true
                problems += m.path
            }
        }
        if (problems.isNotEmpty())
            return failed(if (mismatch) ImportErrorKind.Mismatch else ImportErrorKind.Missing, problems)

        // 2. Copy into staging.
        staging.deleteRecursively()
        for (m in manifest) {
            val e = byPath.getValue(AssetManifest.lower(m.path))
            val to = File(staging, m.path)
            val got = try {
                to.parentFile?.mkdirs()
                onOpen(e.name)
                to.outputStream().use { out -> hash(zf.getInputStream(e), out, progress, cancelled) }
            } catch (x: IOException) {
                null
            }
            if (got != m.sha256) return failed(ImportErrorKind.WriteFailed, listOf(m.path))
        }

        // 3. Verify the copies.
        for (m in manifest) {
            val got = try {
                File(staging, m.path).inputStream().use { hash(it, null, progress, cancelled) }
            } catch (x: IOException) {
                null
            }
            if (got != m.sha256) problems += m.path
        }
        if (problems.isNotEmpty()) return failed(ImportErrorKind.WriteFailed, problems)

        return commit()
    }

    // The stamp last: until it is there, C++ sees "not imported".
    private fun commit(): ImportResult {
        try {
            File(staging, IMPORT_STAMP_FILE).writeText(stampText, Charsets.UTF_8)
        } catch (e: IOException) {
            return failed(ImportErrorKind.CommitFailed, listOf(IMPORT_STAMP_FILE))
        }
        old.deleteRecursively()
        val hadDir = dir.exists()
        if (hadDir) {
            if (!dir.renameTo(old)) return failed(ImportErrorKind.CommitFailed, emptyList())
        } else {
            dir.parentFile?.mkdirs()
        }
        if (!staging.renameTo(dir)) {
            if (hadDir) old.renameTo(dir)   // the previous import comes back
            return failed(ImportErrorKind.CommitFailed, emptyList())
        }
        old.deleteRecursively()
        return ImportResult.Imported(manifest.size)
    }

    private fun failed(kind: ImportErrorKind, paths: List<String>): ImportResult {
        staging.deleteRecursively()
        return ImportResult.Failed(kind, paths.size, paths.take(ERROR_PATHS))
    }

    // SHA-256 of a stream (lower-case hex), copied into `sink` when given;
    // null past maxEntryBytes, whatever the ZIP claimed the size was.
    private fun hash(input: InputStream, sink: OutputStream?, progress: (Float) -> Unit,
                     cancelled: () -> Boolean): String? = input.use {
        val md = MessageDigest.getInstance("SHA-256")
        val buf = ByteArray(CHUNK)
        var read = 0L
        try {
            while (true) {
                if (cancelled()) throw Cancel()
                val n = it.read(buf)
                if (n < 0) break
                read += n
                if (read > maxEntryBytes) throw TooBig()
                md.update(buf, 0, n)
                sink?.write(buf, 0, n)
                doneBytes += n
                if (totalBytes > 0) progress((doneBytes.toFloat() / (totalBytes * 3)).coerceAtMost(1f))
            }
        } catch (e: TooBig) {
            return null
        }
        md.digest().joinToString("") { b -> "%02x".format(b) }
    }
}
