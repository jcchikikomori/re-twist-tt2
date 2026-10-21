# Android Support

## Initial support 

- Use `/skills-md:android` skill to initialize
- Target SDK should be **at least Android 12**
- The port uses mouse to play the game, so emulating it with the touch screen should be straightforward
- Since the port is written in C++, i assume we are going to **use Android NDK package** to compile natively into a Android machine code
- **Default renderer should be OpenGL** since most of the devices uses OpenGL ES, secondary is Vulkan, then last is the Software

### AndroidManifest.xml

- The appplication ID should be: `com.jcchikikomori.retwisttt2`
- Procedurally generate a adaptive logo for the app, including support for Material You
- Have a splash screen activity while loading the game, then the main activity will render the actual game

### Target device

- Tablet
- Phablet
- **Android TV is out of scope for now**, because it requires a controller/remote controller to play the game

### Original game import

- Use native file picker from the system app that is provided by the OEM. If unavailable, we could ask the use which 3rd-party file manager app that supports file picker to use
- **Import the original game but in a zip (compressed) form**, then the port will decompress and will install inside of the game
- The imported assets should be stored on a [scoped storage](https://source.android.com/docs/core/storage/scoped)

## Improvements

- Remove the following options for Android: "Display Mode" & "Resolution"

## Legacy Edition

- Soon, we would have a separate build for Android 5.0 until Android 11.0
- The imported assets should be stored on a Internal Storage (not scoped storage)

## CI / Release

- Include this port as part of the CI/release, as `.apk` file

## As built (2026-10-20)

What the first Android version does, against the points above.

| Point | As built |
| --- | --- |
| Target SDK | `minSdk` 31 (Android 12), `targetSdk` 35. 64-bit ARM (`arm64-v8a`) only, since testing is on real devices |
| Native build | The repository's `CMakeLists.txt` through Gradle and NDK 27.2; SDL3's `SDLActivity` loads `libmain.so` |
| Renderer | **OpenGL ES first**, then Vulkan, then software, on Android only; the desktop order (Vulkan, OpenGL, software) is unchanged |
| Application ID | `com.jcchikikomori.retwisttt2` for releases; debug builds add `.debug`, so they install next to a release ([android-signing.md](../android-signing.md)) |
| Icon | Adaptive, with a monochrome layer for themed icons (Material You); generated from `packaging/linux/tt2.svg` by `tools/gen_android_icon.py` |
| Splash | Android 12's system splash, held until the game draws its first frame (no separate splash activity) |
| Target devices | Tablets and phones in landscape (`sensorLandscape`); not Android TV (no `LEANBACK_LAUNCHER`) |
| Import | A ZIP picked with the system's document picker, verified and copied into the app's private storage ([import-assets.md](import-assets.md#android-as-built-2026-10-17)) |
| Improvements | DISPLAY MODE and RESOLUTION are hidden on Android |
| CI / Release | CI builds a debug APK; Packages builds a release APK signed with the release key, attached to every `v*` release |

## References / Inspiration

- https://github.com/hrydgard/ppsspp
