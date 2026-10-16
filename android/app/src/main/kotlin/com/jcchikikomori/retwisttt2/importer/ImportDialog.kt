package com.jcchikikomori.retwisttt2.importer

import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.KeyEvent
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.ProgressBar
import java.io.File
import java.lang.ref.WeakReference
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

// What the game asked for (MainActivity.openImport).
data class ImportRequest(val manifestText: String, val stampText: String, val assetsDir: File, val askFirst: Boolean)

// The Android import dialog, over the game:
//
//   Prompt --Choose ZIP--> system picker --picked--> Running --> Result --OK--> closed
//     |                       | cancelled -> Prompt     | Cancel -> Prompt
//     |                       | no picker app -> Result(hint)
//     +--Not now / Back--------------------------------------------------> closed
//
// From the launch prompt (askFirst) it opens with the desktop's warning;
// from SETTINGS with the ZIP hint only. Closing always tells the game
// (ImportBridge.nativeFinished). State lives here, not in the Activity, so
// the import keeps going while the app is in the background; rotation
// doesn't recreate the Activity (android:configChanges in the manifest).
object ImportDialog {
    const val PICK_REQUEST = 0x7432   // startActivityForResult code, unique in this app
    private const val TAG = "tt2"
    private const val THEME = android.R.style.Theme_DeviceDefault_Dialog_Alert

    private val main = Handler(Looper.getMainLooper())
    private var request: ImportRequest? = null
    private var activity = WeakReference<Activity>(null)
    private var dialog: AlertDialog? = null
    private var progressBar: ProgressBar? = null
    private val cancelled = AtomicBoolean(false)
    private var lastPercent = -1

    private fun t(name: String) = ImportBridge.text(name)

    // Main thread. A second request while one is open is ignored.
    fun open(a: Activity, r: ImportRequest) {
        if (request != null) return
        request = r
        activity = WeakReference(a)
        Log.i(TAG, "import: dialog open (${if (r.askFirst) "launch" else "settings"})")
        showPrompt()
    }

    // MainActivity.onActivityResult, for PICK_REQUEST. Null: the picker was cancelled.
    fun onPicked(uri: Uri?) {
        if (request == null) return
        Log.i(TAG, "import: picker ${if (uri == null) "cancelled" else "returned a file"}")
        if (uri == null) showPrompt()   // AC-3.5: back to the dialog, no error
        else startImport(uri)
    }

    private fun showPrompt() {
        val r = request ?: return
        val header = t(if (r.askFirst) "PROMPT_HEADER" else "ZIP_PICK_HEADER")
        val body = if (r.askFirst) t("PROMPT_BODY") + "\n\n" + t("ZIP_BODY") else t("ZIP_BODY")
        show { a ->
            AlertDialog.Builder(a, THEME).setTitle(header).setMessage(body)
                .setPositiveButton(t("BUTTON_CHOOSE_ZIP")) { _, _ -> pick() }
                .setNegativeButton(t("BUTTON_NOT_NOW")) { _, _ -> finish(false) }
                .setOnCancelListener { finish(false) }   // Back
                .create()
        }
    }

    private fun pick() {
        val a = activity.get() ?: return finish(false)
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/zip", "application/x-zip-compressed"))
        try {
            @Suppress("DEPRECATION")   // SDLActivity is a plain Activity: no ActivityResult API
            a.startActivityForResult(intent, PICK_REQUEST)
        } catch (e: ActivityNotFoundException) {
            showResult(t("NO_ZIP_PICKER_HEADER"), t("NO_ZIP_PICKER_BODY"), imported = false)
        }
    }

    private fun startImport(uri: Uri) {
        val r = request ?: return
        val app = activity.get()?.applicationContext ?: return finish(false)
        cancelled.set(false)
        lastPercent = -1
        show { a ->
            val bar = ProgressBar(a, null, android.R.attr.progressBarStyleHorizontal).apply {
                max = 100
                isIndeterminate = true   // until the ZIP is in the cache
                val pad = (24 * a.resources.displayMetrics.density).toInt()
                setPadding(pad, 0, pad, 0)
            }
            progressBar = bar
            AlertDialog.Builder(a, THEME).setTitle(t("PROGRESS_HEADER")).setMessage(t("ZIP_READING")).setView(bar)
                .setNegativeButton(t("BUTTON_CANCEL"), null)
                .setCancelable(false)
                .setOnKeyListener { _, key, ev ->
                    if (key == KeyEvent.KEYCODE_BACK && ev.action == KeyEvent.ACTION_UP) cancelled.set(true)
                    key == KeyEvent.KEYCODE_BACK
                }
                .create().also { d ->
                    // Cancel stops the worker; the dialog changes when it has.
                    d.setOnShowListener { d.getButton(AlertDialog.BUTTON_NEGATIVE).setOnClickListener { cancelled.set(true) } }
                }
        }
        thread(name = "tt2-import") {
            val result = runImport(app, uri, r)
            main.post { onResult(result) }
        }
    }

    // Worker thread.
    private fun runImport(app: Context, uri: Uri, r: ImportRequest): ImportResult {
        val manifest = AssetManifest.parse(r.manifestText) ?: return ImportResult.Failed(ImportErrorKind.EmptyManifest)
        val cache = File(app.cacheDir, "import/picked.zip")
        return try {
            val copied = app.contentResolver.openInputStream(uri)?.use {
                ZipImporter.copyToCache(it, cache, cancelled = cancelled::get)
            } ?: false
            when {
                cancelled.get() -> ImportResult.Cancelled
                !copied -> ImportResult.Failed(ImportErrorKind.BadArchive)   // unreadable, or over MAX_ZIP_BYTES
                else -> {
                    main.post { progressBar?.isIndeterminate = false }
                    ZipImporter(manifest, r.stampText, r.assetsDir)
                        .run(cache, progress = ::onProgress, cancelled = cancelled::get)
                }
            }
        } catch (e: Exception) {   // the provider went away, a permission was revoked
            Log.e(TAG, "import: reading the picked file failed", e)
            ImportResult.Failed(ImportErrorKind.BadArchive)
        } finally {
            cache.delete()
        }
    }

    // Worker thread; the dialog only changes when the whole percent does.
    private fun onProgress(p: Float) {
        val pct = (p * 100).toInt()
        if (pct == lastPercent) return
        lastPercent = pct
        main.post {
            progressBar?.progress = pct
            dialog?.setMessage(ImportBridge.progressText(p))
        }
    }

    private fun onResult(result: ImportResult) {
        when (result) {
            is ImportResult.Imported -> {
                Log.i(TAG, "import: ok, ${result.files} files verified")
                showResult(t("DONE_HEADER"), t("DONE_BODY"), imported = true)
            }
            is ImportResult.Failed -> {
                // The dialog stays generic; the details go to the log.
                Log.w(TAG, "import failed: ${result.kind} (${result.count}) ${result.paths}")
                showResult(t("FAILED_HEADER"), ImportBridge.failureText(result), imported = false)
            }
            ImportResult.Cancelled -> showPrompt()
        }
    }

    private fun showResult(header: String, body: String, imported: Boolean) {
        show { a ->
            AlertDialog.Builder(a, THEME).setTitle(header).setMessage(body)
                .setPositiveButton(t("BUTTON_OK")) { _, _ -> finish(imported) }
                .setOnCancelListener { finish(imported) }
                .create()
        }
    }

    private fun finish(imported: Boolean) {
        Log.i(TAG, "import: dialog closed (${if (imported) "imported" else "nothing imported"})")
        dialog?.dismiss()
        dialog = null
        progressBar = null
        request = null
        ImportBridge.nativeFinished(imported)
    }

    // Replaces the open dialog. The game runs full screen: the dialog is
    // shown unfocusable first so the system bars stay hidden, then takes
    // focus with the same immersive flags.
    private fun show(build: (Activity) -> AlertDialog) {
        val a = activity.get() ?: return finish(false)
        if (a.isFinishing || a.isDestroyed) return finish(false)
        dialog?.dismiss()
        val d = build(a)
        d.setCanceledOnTouchOutside(false)
        val w = d.window
        w?.setFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE, WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
        d.show()
        w?.insetsController?.let {
            it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            it.hide(WindowInsets.Type.systemBars())
        }
        w?.clearFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE)
        dialog = d
    }
}
