# Android release signing

Every release APK is signed with one key, the **release key**. Android installs an update only when it's signed
with the same key as the installed app, so this key is what lets a player go from 0.6.0 to 0.7.0 and keep their
saves and imported assets.

- The key lives in a keystore file on the maintainer's machine and, base64-encoded, in the repository's GitHub
  secrets. It is **never committed** (`android/.gitignore` ignores `*.jks` and `*.keystore`).
- **Back it up.** If the keystore or its password is lost, the next release can't update the installed app:
  every player has to uninstall it first, losing their saves and imported assets.

## 1. Create the keystore (once)

```bash
keytool -genkeypair -v -storetype PKCS12 \
  -keystore ~/re-twist-tt2-release.jks -alias re-twist-tt2 \
  -keyalg RSA -keysize 4096 -validity 10000
```

`keytool` comes with the JDK. It asks for a password and a name. A PKCS12 keystore has one password for the store
and the key, and the build uses it for both. 10,000 days is about 27 years: an APK can't be updated once its
key has expired.

Copy the `.jks` file and its password to two places that aren't this machine (for example a password manager and
an offline drive).

## 2. Give CI the key

Run these in your own terminal, so the password never shows up anywhere else:

```bash
base64 -w0 ~/re-twist-tt2-release.jks | gh secret set TT2_KEYSTORE_BASE64
gh secret set TT2_KEYSTORE_PASSWORD     # prompts for the value
gh secret set TT2_KEY_ALIAS --body re-twist-tt2
```

`.github/workflows/packages.yml` decodes the keystore into the runner's temp folder, builds
`assembleRelease`, deletes it, and checks the result with `apksigner verify`. The job fails when the secrets
are missing, rather than publish an unsigned APK. GitHub masks secret values in the logs.

## 3. Release builds on your machine (optional)

Gradle reads the same three names from `~/.gradle/gradle.properties` (outside the repository) or from the
environment:

```properties
TT2_KEYSTORE=/home/you/re-twist-tt2-release.jks
TT2_KEYSTORE_PASSWORD=...
TT2_KEY_ALIAS=re-twist-tt2
```

`chmod 600 ~/.gradle/gradle.properties`, then `./gradlew assembleRelease` in `android/` writes
`app/build/outputs/apk/release/app-release.apk`. Without the values it writes `app-release-unsigned.apk`, which
no device installs. Setting only some of them fails the build.

## Debug and release builds side by side

Debug builds are signed with each machine's own debug key, so they could never update a release install, or the
other way round. They are a separate app instead: application ID `com.jcchikikomori.retwisttt2.debug`, shown as
"re-twist-tt2 (debug)" in the launcher, with their own saves and imported assets. Both can be installed at once.

```bash
adb shell am start -n com.jcchikikomori.retwisttt2.debug/com.jcchikikomori.retwisttt2.MainActivity
```
