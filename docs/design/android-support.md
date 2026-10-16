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

## Legacy Edition

- Soon, we would have a separate build for Android 5.0 until Android 11.0
- The imported assets should be stored on a Internal Storage (not scoped storage)

## CI / Release

- Include this port as part of the CI/release, as `.apk` file

## References / Inspiration

- https://github.com/hrydgard/ppsspp
