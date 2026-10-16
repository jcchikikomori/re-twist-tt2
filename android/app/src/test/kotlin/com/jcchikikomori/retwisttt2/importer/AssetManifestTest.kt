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
        assertEquals(323, m!!.size)
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
