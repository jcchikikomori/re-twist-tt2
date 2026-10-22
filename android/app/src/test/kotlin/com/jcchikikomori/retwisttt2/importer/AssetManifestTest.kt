package com.jcchikikomori.retwisttt2.importer

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AssetManifestTest {
    private val a = "a".repeat(64)
    private val b = "b".repeat(64)
    private fun manifest(vararg lines: String) =
        (listOf(AssetManifest.TITLE, AssetManifest.VERSION) + lines).joinToString("\n", postfix = "\n")

    @Test
    fun parsesEntriesInOrder() {
        val m = AssetManifest.parse(manifest("$a  images/gui/ok.png", "$b  sound/click.ogg"))
        assertEquals(listOf(AssetManifest.Entry("images/gui/ok.png", a), AssetManifest.Entry("sound/click.ogg", b)), m)
    }

    @Test
    fun acceptsCrlfBlankLinesAndOtherComments() {
        val text = "${AssetManifest.TITLE}\r\n# made by tt2-assets\r\n${AssetManifest.VERSION}\r\n\r\n$a  fonts/x.xml\r\n"
        assertEquals(listOf(AssetManifest.Entry("fonts/x.xml", a)), AssetManifest.parse(text))
    }

    @Test
    fun readsTheCommittedManifest() {
        // Paths and hashes only (data/original-assets.sha256); the test runs in android/app.
        val m = AssetManifest.parse(File("../../data/original-assets.sha256").readText())
        assertNotNull(m)
        assertEquals(329, m!!.size)   // 323 before the Crossword scene, plus its 6 files
    }

    // ui/tests/import-contract/import-roots.txt, one root per line. The C++
    // engine's test_asset_manifest.cpp reads the same file, so the two
    // IMPORT_ROOTS lists can't drift apart.
    private val sharedRoots = File("../../ui/tests/import-contract/import-roots.txt").readLines()

    @Test
    fun importRootsMatchTheSharedList() {
        assertEquals(11, sharedRoots.size)   // the import script's 10 folders, plus images/crosswordscene
        assertEquals(sharedRoots, AssetManifest.IMPORT_ROOTS)   // same folders, same order
    }

    @Test
    fun acceptsAnEntryUnderEverySharedRoot() {
        assertEquals(11, sharedRoots.size)
        for (root in sharedRoots) {
            val parsed = AssetManifest.parse(manifest("$a  $root/a.png"))
            assertEquals(root, listOf(AssetManifest.Entry("$root/a.png", a)), parsed)
        }
    }

    @Test
    fun rejectsAnEntryUnderAnUnknownRoot() {
        // Near misses of the Crossword scene's folder: only images/crosswordscene/ itself is a root.
        assertNotNull(AssetManifest.parse(manifest("$a  images/crosswordscene/background.png")))
        for (p in listOf("images/crosswordscene", "images/crosswordscenes/background.png",
                         "images/crossword/background.png", "crosswordscene/background.png",
                         "images/creditsscene/background.png"))
            assertNull(p, AssetManifest.parse(manifest("$a  $p")))
    }

    @Test
    fun rejectsABadHeader() {
        assertNull(AssetManifest.parse(""))
        assertNull(AssetManifest.parse("$a  fonts/x.xml\n"))                                  // no title
        assertNull(AssetManifest.parse("${AssetManifest.TITLE}\n$a  fonts/x.xml\n"))          // entry before version
        assertNull(AssetManifest.parse("${AssetManifest.TITLE}\n# version=2\n$a  fonts/x.xml\n"))
        assertNull(AssetManifest.parse("${AssetManifest.TITLE}\n"))                            // no version
    }

    @Test
    fun rejectsBadEntries() {
        assertNull(AssetManifest.parse(manifest("${a.uppercase()}  fonts/x.xml")))   // one canonical hex form
        assertNull(AssetManifest.parse(manifest("${a.drop(1)}  fonts/x.xml")))
        assertNull(AssetManifest.parse(manifest("$a fonts/x.xml")))                   // one space
        assertNull(AssetManifest.parse(manifest("$a  ../fonts/x.xml")))
        assertNull(AssetManifest.parse(manifest("$a  /fonts/x.xml")))
        assertNull(AssetManifest.parse(manifest("$a  properties/resources.xml")))     // outside the import roots
        assertNull(AssetManifest.parse(manifest("$a  sound/texttwist2_RADRM.dll")))   // DRM, never
        assertNull(AssetManifest.parse(manifest("$a  sound/readme.nfo")))
        assertNull(AssetManifest.parse(manifest("$a  fonts/x.xml", "$b  FONTS/X.XML")))   // duplicate ignoring case
    }

    @Test
    fun pathRulesMatchTheCpp() {
        assertTrue(AssetManifest.isSafeRelative("images/gui/ok.png"))
        for (bad in listOf("", "/a", "a/", "a//b", "./a", "a/../b", "c:a", "a\\b", "a\u0001b"))
            assertFalse(bad, AssetManifest.isSafeRelative(bad))
        assertTrue(AssetManifest.underImportRoot("images/gui/ok.png"))
        assertFalse(AssetManifest.underImportRoot("images/gui"))
        assertFalse(AssetManifest.underImportRoot("images/guide/ok.png"))
        assertTrue(AssetManifest.isExcluded("music/Thumbs.db"))
        assertTrue(AssetManifest.isExcluded("x/WORDS.TREE"))
        assertFalse(AssetManifest.isExcluded("music/theme.ogg"))
        assertEquals("istanbul", AssetManifest.lower("ISTANBUL"))   // ASCII, whatever the locale
    }
}
