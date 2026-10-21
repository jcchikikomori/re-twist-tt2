// android_splash.hpp: one JNI call into MainActivity.
#include "android_splash.hpp"

#include <jni.h>

#include <cstdio>

#include <SDL3/SDL.h>

namespace tt2android {

void first_frame_shown() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());   // a local reference
    if (!env || !activity) return;
    jclass cls = env->GetObjectClass(activity);
    jmethodID m = env->GetMethodID(cls, "onFirstFrame", "()V");
    if (m) env->CallVoidMethod(activity, m);
    if (env->ExceptionCheck()) {
        std::fprintf(stderr, "splash: JNI onFirstFrame failed\n");
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
}

}  // namespace tt2android
