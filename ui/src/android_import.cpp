// JNI both ways for the Kotlin import dialog (android_import.hpp).
//   C++ -> Kotlin: MainActivity.openImport(manifest, stamp, assetsDir, askFirst)
//   Kotlin -> C++: ImportBridge.native* below, for the shared import text
//                  (import_prompt.hpp, T3.6) and the "dialog closed" event.
#include "android_import.hpp"

#include <jni.h>

#include <atomic>
#include <cstdio>
#include <string>

#include "tt2ui/import_prompt.hpp"

namespace {

std::atomic<Uint32> g_import_event{0};

std::string from_java(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

// The text is ASCII (import_prompt.hpp, manifest paths), which modified
// UTF-8 passes through unchanged.
jstring to_java(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }

bool clear_exception(JNIEnv* env, const char* what) {
    if (!env->ExceptionCheck()) return false;
    std::fprintf(stderr, "import: JNI %s failed\n", what);
    env->ExceptionDescribe();   // the stack trace goes to logcat
    env->ExceptionClear();
    return true;
}

}  // namespace

namespace tt2android {

Uint32 register_import_event() {
    const Uint32 type = SDL_RegisterEvents(1);
    g_import_event = type == 0 || type == static_cast<Uint32>(-1) ? 0 : type;
    return g_import_event;
}

bool open_import(const std::string& manifest_text, const std::string& stamp_text, const std::string& assets_dir,
                 bool ask_first) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());   // a local reference
    if (!env || !activity) return false;
    bool ok = false;
    jclass cls = env->GetObjectClass(activity);
    jmethodID open = env->GetMethodID(cls, "openImport", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Z)V");
    if (!clear_exception(env, "openImport lookup") && open) {
        jstring m = to_java(env, manifest_text), s = to_java(env, stamp_text), d = to_java(env, assets_dir);
        env->CallVoidMethod(activity, open, m, s, d, static_cast<jboolean>(ask_first));
        ok = !clear_exception(env, "openImport");
        env->DeleteLocalRef(m);
        env->DeleteLocalRef(s);
        env->DeleteLocalRef(d);
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ok;
}

}  // namespace tt2android

// com.jcchikikomori.retwisttt2.importer.ImportBridge (Kotlin, @JvmStatic external).
extern "C" {

JNIEXPORT jstring JNICALL Java_com_jcchikikomori_retwisttt2_importer_ImportBridge_nativeText(JNIEnv* env, jclass,
                                                                                            jstring name) {
    return to_java(env, tt2ui::import_text(from_java(env, name)));
}

JNIEXPORT jstring JNICALL Java_com_jcchikikomori_retwisttt2_importer_ImportBridge_nativeProgressText(JNIEnv* env,
                                                                                                    jclass,
                                                                                                    jfloat progress) {
    return to_java(env, tt2ui::import_progress_text(progress));
}

JNIEXPORT jstring JNICALL Java_com_jcchikikomori_retwisttt2_importer_ImportBridge_nativeFailureText(
    JNIEnv* env, jclass, jstring kind, jint count, jobjectArray paths) {
    tt2ui::ImportError e;
    if (!tt2ui::import_error_kind_from_name(from_java(env, kind), e.kind)) {
        std::fprintf(stderr, "import: unknown error kind %s\n", from_java(env, kind).c_str());
        e.kind = tt2ui::ImportError::Kind::WriteFailed;   // the generic "nothing changed" text
    }
    e.count = count < 0 ? 0 : static_cast<std::size_t>(count);
    const jsize n = paths ? env->GetArrayLength(paths) : 0;
    for (jsize i = 0; i < n && e.paths.size() < tt2ui::IMPORT_ERROR_PATHS; ++i) {
        auto p = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
        e.paths.push_back(from_java(env, p));
        env->DeleteLocalRef(p);
    }
    return to_java(env, tt2ui::import_failure_text(e));
}

// The dialog closed; runs on the Android UI thread. SDL_PushEvent is
// thread-safe, and the game's loop does the rest (main.cpp).
JNIEXPORT void JNICALL Java_com_jcchikikomori_retwisttt2_importer_ImportBridge_nativeFinished(JNIEnv*, jclass,
                                                                                             jboolean imported) {
    const Uint32 type = g_import_event;
    if (type == 0) return;
    SDL_Event ev{};
    ev.type = type;
    ev.user.code = imported ? 1 : 0;
    SDL_PushEvent(&ev);
}

}  // extern "C"
