package com.jcchikikomori.retwisttt2

import android.content.Intent
import com.jcchikikomori.retwisttt2.importer.ImportDialog
import com.jcchikikomori.retwisttt2.importer.ImportRequest
import java.io.File
import org.libsdl.app.SDLActivity

// SDLActivity (SDL3's Java class) creates the window surface, loads
// libSDL3.so and libmain.so (getLibraries()), and calls SDL_main in
// ui/src/main.cpp on its own thread.
class MainActivity : SDLActivity() {
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
