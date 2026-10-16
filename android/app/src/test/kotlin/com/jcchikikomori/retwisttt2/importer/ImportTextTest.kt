package com.jcchikikomori.retwisttt2.importer

import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Test

// T3.6: every import message lives in ui/include/tt2ui/import_prompt.hpp.
// The Kotlin sources and res/ only name the constants, never copy the text.
class ImportTextTest {
    private val header = File("../../ui/include/tt2ui/import_prompt.hpp").readText()
    private val ns = header.substringAfter("namespace import_strings {").substringBefore("}  // namespace import_strings")

    // Every constant's text pieces (C++ string concatenation), escapes kept as written.
    private val pieces = Regex("""constexpr const char\* (\w+) =\s*((?:"(?:[^"\\]|\\.)*"\s*)+);""").findAll(ns)
        .flatMap { m -> Regex(""""((?:[^"\\]|\\.)*)"""").findAll(m.groupValues[2]).map { it.groupValues[1] } }
        .toList()

    private val sources = (File("src/main/kotlin").walkTopDown() + File("src/main/res").walkTopDown())
        .filter { it.isFile }.associate { it.path to it.readText() }

    @Test
    fun theHeaderHasTheConstantsKotlinAsksFor() {
        val names = Regex("""constexpr const char\* (\w+) =""").findAll(ns).map { it.groupValues[1] }.toSet()
        val asked = Regex("""t\("(\w+)"\)|"(\w+)" else "(\w+)"""").findAll(sources.values.joinToString("\n"))
            .flatMap { it.groupValues.drop(1) }.filter { it.isNotEmpty() }.toSet()
        assertTrue("asked: $asked", asked.size >= 10)
        assertTrue("missing in import_prompt.hpp: ${asked - names}", names.containsAll(asked))
    }

    @Test
    fun noImportTextIsCopiedIntoKotlinOrResources() {
        assertTrue(pieces.size >= 19)
        for (p in pieces.filter { it.length >= 6 })
            for ((path, text) in sources)
                assertTrue("$path copies \"$p\"", !text.contains("\"$p") && !text.contains(">$p"))
    }
}
