package com.jcchikikomori.retwisttt2

import android.content.Intent
import android.os.Bundle
import android.os.SystemClock
import android.view.View
import android.view.ViewTreeObserver
import com.jcchikikomori.retwisttt2.importer.ImportDialog
import com.jcchikikomori.retwisttt2.importer.ImportRequest
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import org.libsdl.app.SDLActivity

// SDLActivity (SDL3's Java class) creates the window surface, loads
// libSDL3.so and libmain.so (getLibraries()), and calls SDL_main in
// ui/src/main.cpp on its own thread.
class MainActivity : SDLActivity() {
    private companion object {
        // Never hold the splash longer than this, whatever the game does.
        const val SPLASH_MAX_MS = 10_000L
    }

    private val firstFrame = AtomicBoolean(false)

    // T4.2: Android 12's splash (AppTheme) goes away at the window's first
    // draw. Holding that draw keeps it up over the loading (dictionaries,
    // renderer), where SDL's surface would otherwise show black, until the
    // game reports its first frame. SurfaceView still creates its surface
    // while the draw is held, so the game starts drawing underneath.
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val start = SystemClock.uptimeMillis()
        val content = findViewById<View>(android.R.id.content)
        content.viewTreeObserver.addOnPreDrawListener(object : ViewTreeObserver.OnPreDrawListener {
            override fun onPreDraw(): Boolean {
                if (!firstFrame.get() && SystemClock.uptimeMillis() - start < SPLASH_MAX_MS) return false
                content.viewTreeObserver.removeOnPreDrawListener(this)
                return true
            }
        })
    }

    // Called once by the game's thread through JNI (ui/src/android_splash.cpp).
    @Suppress("unused")
    fun onFirstFrame() {
        firstFrame.set(true)
        runOnUiThread { findViewById<View>(android.R.id.content).invalidate() }
    }

    // Called by the game's thread through JNI (ui/src/android_import.cpp):
    // the launch prompt or SETTINGS -> IMPORT. The dialog lives on the UI thread.
    @Suppress("unused")
    fun openImport(manifestText: String, stampText: String, assetsDir: String, askFirst: Boolean) {
        runOnUiThread { ImportDialog.open(this, ImportRequest(manifestText, stampText, File(assetsDir), askFirst)) }
    }

    @Deprecated("SDLActivity is a plain Activity: no ActivityResult API")
    @Suppress("DEPRECATION")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == ImportDialog.PICK_REQUEST) ImportDialog.onPicked(if (resultCode == RESULT_OK) data?.data else null)
    }
}
