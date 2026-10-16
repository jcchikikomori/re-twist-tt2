package com.jcchikikomori.retwisttt2.importer

// The C++ side of the import dialog, in libmain.so (ui/src/android_import.cpp).
// Every message the dialog shows comes from ui/include/tt2ui/import_prompt.hpp
// through these calls, so the Kotlin sources and res/ hold no import text
// (ImportTextTest checks it).
object ImportBridge {
    // An import_strings constant by name ("PROMPT_HEADER"); "" when unknown.
    @JvmStatic external fun nativeText(name: String): String

    // tt2ui::import_progress_text(), the percentage line.
    @JvmStatic external fun nativeProgressText(progress: Float): String

    // The desktop's failure text for the same error (AC-3.8).
    @JvmStatic external fun nativeFailureText(kind: String, count: Int, paths: Array<String>): String

    // The dialog closed; the game re-checks the folder and reloads its art.
    @JvmStatic external fun nativeFinished(imported: Boolean)

    fun text(name: String) = nativeText(name)
    fun progressText(progress: Float) = nativeProgressText(progress)
    fun failureText(f: ImportResult.Failed) = nativeFailureText(f.kind.name, f.count, f.paths.toTypedArray())
}
