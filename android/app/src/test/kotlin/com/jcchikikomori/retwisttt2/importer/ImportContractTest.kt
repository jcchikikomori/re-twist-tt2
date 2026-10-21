package com.jcchikikomori.retwisttt2.importer

import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

// The import contract with the game (ui/tests/import-contract/README.md).
// The C++ side, test_import_contract.cpp, checks that this exact layout
// passes import_status().
class ImportContractTest {
    @get:Rule
    val tmp = TemporaryFolder()

    private val contract = File("../../ui/tests/import-contract")
    private val manifest = AssetManifest.parse(File(contract, "manifest.sha256").readText())
    private val golden = File(contract, ZipImporter.IMPORT_STAMP_FILE).readBytes()

    @Test
    fun importingTheContractZipGivesTheContractLayout() {
        val m = manifest ?: error("contract manifest did not parse")
        assertEquals(3, m.size)
        val zip = File(tmp.root, "contract.zip")
        ZipOutputStream(zip.outputStream()).use { z ->
            z.putNextEntry(ZipEntry("Text Twist 2/${AssetManifest.INSTALL_PROBE}"))
            z.write("<x/>".toByteArray())
            for (e in m) {
                z.putNextEntry(ZipEntry("Text Twist 2/${e.path}"))
                z.write(e.path.toByteArray(Charsets.UTF_8))   // a file's bytes are its path
            }
        }
        val dir = File(tmp.root, "files/re-twist-tt2/imported-assets")
        val stamp = String(golden, Charsets.UTF_8)
        assertEquals(ImportResult.Imported(3), ZipImporter(m, stamp, dir).run(zip))

        assertArrayEquals(golden, File(dir, ZipImporter.IMPORT_STAMP_FILE).readBytes())   // byte for byte
        for (e in m) assertEquals(e.path, File(dir, e.path).readText())
        val written = dir.walkTopDown().filter { it.isFile }.map { it.relativeTo(dir).invariantSeparatorsPath }.toSet()
        assertEquals(m.map { it.path }.toSet() + ZipImporter.IMPORT_STAMP_FILE, written)
        assertEquals(listOf("imported-assets"), dir.parentFile.list()!!.toList())   // no .staging or .old left
    }

    @Test
    fun theStampFileNameMatchesTheCpp() {
        val header = File("../../ui/include/tt2ui/asset_import.hpp").readText()
        assertEquals(true, header.contains("IMPORT_STAMP_FILE = \"${ZipImporter.IMPORT_STAMP_FILE}\""))
        assertEquals(true, header.contains("IMPORT_ERROR_PATHS = ${ZipImporter.ERROR_PATHS};"))
    }
}
