package com.jcchikikomori.retwisttt2.importer

import java.io.ByteArrayInputStream
import java.io.File
import java.security.MessageDigest
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import kotlin.random.Random
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

// Fixtures are random bytes under made-up names, never real assets.
class ZipImporterTest {
    @get:Rule
    val tmp = TemporaryFolder()

    private val rnd = Random(2008)
    private val files = linkedMapOf(
        "images/gui/button_ok.png" to rnd.nextBytes(3000),
        "fonts/title.xml" to rnd.nextBytes(500),
        "sound/click.ogg" to rnd.nextBytes(70_000),   // more than one 64 KiB chunk
    )
    private val manifest = files.map { (p, b) -> AssetManifest.Entry(p, sha(b)) }
    private val stamp = "format=re-twist-tt2 asset import\nversion=1\nmanifest=${"c".repeat(64)}\nfiles=3\n"

    private val assets by lazy { File(tmp.root, "files/re-twist-tt2/imported-assets") }

    private fun sha(b: ByteArray) = MessageDigest.getInstance("SHA-256").digest(b).joinToString("") { "%02x".format(it) }

    private fun zip(entries: Map<String, ByteArray>, name: String = "game.zip"): File {
        val f = File(tmp.root, name)
        ZipOutputStream(f.outputStream()).use { z ->
            for ((n, b) in entries) {
                z.putNextEntry(ZipEntry(n))
                z.write(b)
                z.closeEntry()
            }
        }
        return f
    }

    private fun install(prefix: String = "Text Twist 2/", extra: Map<String, ByteArray> = emptyMap()) =
        (mapOf("properties/resources.xml" to "<x/>".toByteArray()) + files + extra).mapKeys { prefix + it.key }

    private fun importer(m: List<AssetManifest.Entry> = manifest, maxEntry: Long = ZipImporter.MAX_ENTRY_BYTES,
                         maxTotal: Long = ZipImporter.MAX_TOTAL_BYTES) =
        ZipImporter(m, stamp, assets, maxEntry, maxTotal)

    private fun previousImport() {
        assets.mkdirs()
        File(assets, "import-stamp.txt").writeText("old stamp")
    }

    private fun assertPreviousImportKept() {
        assertEquals("old stamp", File(assets, "import-stamp.txt").readText())
        assertFalse(ZipImporter.stagingDir(assets).exists())
        assertFalse(ZipImporter.oldDir(assets).exists())
    }

    private fun assertImported() {
        for ((p, b) in files) assertArrayEquals(p, b, File(assets, p).readBytes())
        assertEquals(stamp, File(assets, "import-stamp.txt").readText())   // byte for byte
        assertFalse(ZipImporter.stagingDir(assets).exists())
        assertFalse(ZipImporter.oldDir(assets).exists())
    }

    // Positive cases.

    @Test
    fun importsAZipOfTheFolder() {
        assertEquals(ImportResult.Imported(3), importer().run(zip(install())))
        assertImported()
    }

    @Test
    fun importsAZipOfTheFolderContents() {
        assertEquals(ImportResult.Imported(3), importer().run(zip(install(prefix = ""))))
        assertImported()
    }

    @Test
    fun replacesAPreviousImport() {
        previousImport()
        File(assets, "stray.txt").writeText("from an older layout")
        assertEquals(ImportResult.Imported(3), importer().run(zip(install())))
        assertImported()
        assertFalse(File(assets, "stray.txt").exists())
    }

    @Test
    fun acceptsWindowsSeparatorsAndOtherCase() {
        val entries = install(prefix = "TEXT TWIST 2\\").mapKeys { it.key.replace('/', '\\').uppercase() }
        assertEquals(ImportResult.Imported(3), importer().run(zip(entries)))
        assertImported()   // written under the manifest's names
    }

    @Test
    fun ignoresExtraFilesAndNeverOpensDrmOrCode() {
        val extra = mapOf(
            "texttwist2_RADRM.dll" to rnd.nextBytes(10), "Text Twist 2.nfo" to rnd.nextBytes(10),
            "Text Twist 2.exe" to rnd.nextBytes(10), "DamBeaversWords.tree" to rnd.nextBytes(10),
            "images/gui/Thumbs.db" to rnd.nextBytes(10),
        )
        val opened = mutableListOf<String>()
        assertEquals(ImportResult.Imported(3), importer().run(zip(install(extra = extra)), onOpen = { opened += it }))
        assertImported()
        assertEquals(files.keys.map { "Text Twist 2/$it" }.toSet(), opened.toSet())
        val written = assets.walkTopDown().filter { it.isFile }.map { it.relativeTo(assets).invariantSeparatorsPath }
        assertEquals(files.keys + "import-stamp.txt", written.toSet())
    }

    @Test
    fun progressRisesToOne() {
        val seen = mutableListOf<Float>()
        importer().run(zip(install()), progress = { seen += it })
        assertEquals(0f, seen.first())
        assertEquals(1f, seen.last(), 1e-4f)
        assertEquals(seen.sorted(), seen)
    }

    // Negative cases.

    @Test
    fun oneChangedByteFailsAndKeepsThePreviousImport() {
        previousImport()
        val changed = files.getValue("sound/click.ogg").copyOf().also { it[100] = (it[100] + 1).toByte() }
        val r = importer().run(zip(install(extra = mapOf("sound/click.ogg" to changed))))
        assertEquals(ImportResult.Failed(ImportErrorKind.Mismatch, 1, listOf("sound/click.ogg")), r)
        assertPreviousImportKept()
    }

    @Test
    fun aMissingFileFails() {
        previousImport()
        val entries = install().filterKeys { !it.endsWith("fonts/title.xml") }
        assertEquals(ImportResult.Failed(ImportErrorKind.Missing, 1, listOf("fonts/title.xml")), importer().run(zip(entries)))
        assertPreviousImportKept()
    }

    @Test
    fun aMismatchOutranksAMissingFileLikeTheDesktop() {
        val entries = install(extra = mapOf("images/gui/button_ok.png" to byteArrayOf(1))).filterKeys {
            !it.endsWith("fonts/title.xml")
        }
        assertEquals(ImportResult.Failed(ImportErrorKind.Mismatch, 2, listOf("images/gui/button_ok.png", "fonts/title.xml")),
                     importer().run(zip(entries)))
    }

    @Test
    fun aZipSlipEntryIsRejectedAndNothingLeavesTheFolder() {
        previousImport()
        for (evil in listOf("../evil", "Text Twist 2/../../evil", "..\\evil", "/etc/evil", "C:/evil")) {
            val r = importer().run(zip(install() + (evil to rnd.nextBytes(10))))
            assertEquals(evil, ImportResult.Failed(ImportErrorKind.BadArchive), r)
            assertPreviousImportKept()
        }
        assertTrue(tmp.root.walkTopDown().none { it.name == "evil" })
        assertFalse(File(tmp.root.parentFile, "evil").exists())
    }

    @Test
    fun anOversizedEntryIsAMismatchNotAnAllocation() {
        // 1 MiB of zeros deflates to about 1 KiB: a tiny ZIP bomb.
        val bomb = mapOf("sound/click.ogg" to ByteArray(1 shl 20))
        val r = importer(maxEntry = 100_000).run(zip(install(extra = bomb)))
        assertEquals(ImportResult.Failed(ImportErrorKind.Mismatch, 1, listOf("sound/click.ogg")), r)
    }

    @Test
    fun tooManyBytesInAllIsRejected() {
        assertEquals(ImportResult.Failed(ImportErrorKind.BadArchive), importer(maxTotal = 10_000).run(zip(install())))
    }

    @Test
    fun twoEntriesForOneFileAreRejected() {
        val r = importer().run(zip(install() + ("Text Twist 2/FONTS/TITLE.XML" to files.getValue("fonts/title.xml"))))
        assertEquals(ImportResult.Failed(ImportErrorKind.BadArchive), r)
    }

    @Test
    fun notAZipOrADamagedZipIsRejected() {
        val random = File(tmp.root, "random.zip").apply { writeBytes(rnd.nextBytes(4096)) }
        assertEquals(ImportResult.Failed(ImportErrorKind.BadArchive), importer().run(random))
        val empty = File(tmp.root, "empty.zip").apply { writeBytes(ByteArray(0)) }
        assertEquals(ImportResult.Failed(ImportErrorKind.BadArchive), importer().run(empty))
        val good = zip(install()).readBytes()
        val cut = File(tmp.root, "cut.zip").apply { writeBytes(good.copyOf(good.size / 2)) }
        assertEquals(ImportResult.Failed(ImportErrorKind.BadArchive), importer().run(cut))
        assertFalse(assets.exists())
    }

    @Test
    fun anEmptyZipOrOneWithoutTheProbeIsNotAnInstall() {
        assertEquals(ImportResult.Failed(ImportErrorKind.NotAnInstall), importer().run(zip(emptyMap())))
        assertEquals(ImportResult.Failed(ImportErrorKind.NotAnInstall), importer().run(zip(files)))
    }

    @Test
    fun anEmptyManifestImportsNothing() {
        assertEquals(ImportResult.Failed(ImportErrorKind.EmptyManifest), importer(m = emptyList()).run(zip(install())))
        assertFalse(assets.exists())
    }

    @Test
    fun cancellingKeepsThePreviousImport() {
        previousImport()
        var calls = 0
        assertEquals(ImportResult.Cancelled, importer().run(zip(install()), cancelled = { ++calls > 5 }))
        assertPreviousImportKept()
    }

    @Test
    fun copyToCacheStopsAtItsCap() {
        val to = File(tmp.root, "cache/pick.zip")
        assertTrue(ZipImporter.copyToCache(ByteArrayInputStream(ByteArray(1000)), to, maxBytes = 1000))
        assertEquals(1000L, to.length())
        assertFalse(ZipImporter.copyToCache(ByteArrayInputStream(ByteArray(1001)), to, maxBytes = 1000))
        assertFalse(to.exists())
        assertFalse(ZipImporter.copyToCache(ByteArrayInputStream(ByteArray(10)), to, cancelled = { true }))
        assertFalse(to.exists())
    }

    @Test
    fun errorKindsMatchTheCppNames() {
        // tt2ui::ImportError::Kind in ui/include/tt2ui/asset_import.hpp, in order.
        val header = File("../../ui/include/tt2ui/asset_import.hpp").readText()
        val body = Regex("""enum class Kind \{(.*?)\};""", RegexOption.DOT_MATCHES_ALL).find(header)!!.groupValues[1]
        val cpp = Regex("""^\s*(\w+),""", RegexOption.MULTILINE).findAll(body).map { it.groupValues[1] }.toList()
        assertEquals(cpp, ImportErrorKind.entries.map { it.name })
    }
}
