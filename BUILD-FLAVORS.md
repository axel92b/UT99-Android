# UT99 Android build flavors

The project contains two platform flavors from one shared Java/JNI/native codebase.

- `normal`: standard Android + OUYA, application ID `com.ast.ut99`, minSdk 16.
- `automotive`: Android Automotive OS, application ID `com.ast.ut99android`.

`automotiveDebug` is intentionally disabled. This leaves `normalDebug` as the only debug/run variant, so a fresh Android Studio import and the normal Run/Play workflow use the standard Android/OUYA build by default.

## Build commands

Normal Android / OUYA APK:

```text
./gradlew buildNormalApk
```

Equivalent Gradle task:

```text
./gradlew assembleNormalDebug
```

Android Automotive release AAB:

```text
./gradlew buildAutomotiveAab
```

Equivalent Gradle task:

```text
./gradlew bundleAutomotiveRelease
```

Outputs are written below `app/build/outputs/`.

## GitHub Actions release APK

The **Build release APK** workflow runs on pushes to `master` and can also be
started from **Actions > Build release APK > Run workflow**. It builds
`assembleNormalRelease` for both `armeabi-v7a` and `arm64-v8a`, signs the APK with
a reusable Android debug keystore, and uploads `UT99-normal-release.apk` in the
run's `UT99-normal-release-<run number>` artifact. Artifacts are retained for
14 days. No GitHub Release is published.

This is a release build, not a debuggable build. Debug signing is for testing,
not production distribution. Local release builds and the Automotive flavor's
signing configuration are unchanged.

Gradle wrappers are checked in a dedicated validation step. The unused wrapper
in SDL's Android sample project is allowed by its exact SHA-256 checksum,
verified against SDL's upstream `release-2.28.5` tag. The build executes only
the repository-root Gradle wrapper.

### One-time signing setup

Configure the repository's **Settings > Secrets and variables > Actions** with
a secret named `ANDROID_DEBUG_KEYSTORE_BASE64`. It must contain a Base64-encoded
debug keystore with alias `androiddebugkey` and both passwords set to `android`.
The workflow fails early if this secret is missing.

Reuse the keystore that signed your existing test APK if you need to update
that installation. If you do not already have one, generate a keystore once,
outside the repository:

```sh
mkdir -p "$HOME/.android"
keytool -genkeypair \
  -keystore "$HOME/.android/ut99-ci-debug.keystore" \
  -storetype JKS \
  -alias androiddebugkey \
  -storepass android \
  -keypass android \
  -keyalg RSA \
  -keysize 2048 \
  -validity 10000 \
  -dname "CN=Android Debug,O=Android,C=US"
```

With GitHub CLI authenticated, upload that keystore directly as the secret:

```sh
base64 < "$HOME/.android/ut99-ci-debug.keystore" | tr -d '\r\n' |
  gh secret set ANDROID_DEBUG_KEYSTORE_BASE64 --repo axel92b/UT99-Android
```

Substitute your existing debug keystore path if reusing one. Keep a backup and
do not regenerate the key for each build or commit it to the repository.
Successive workflow APKs can update one another because they keep the same
application ID and signing key, provided `versionCode` is not decreased.
An APK signed with a different key, including an upstream release, cannot be
updated in place using this key; uninstalling it can remove its game data.

## Automotive-specific behavior

The Automotive flavor keeps the shared game/engine sources but activates AAOS behavior at runtime when `android.hardware.type.automotive` is present:

- Android immersive-mode forcing is disabled so vehicle-owned system bars remain under AAOS control.
- Automotive lifecycle cleanup releases pending overlay/UI callbacks when the game is backgrounded.
- SDL touch normalization uses the actual visible `SurfaceView` dimensions instead of the physical display dimensions. This avoids touch-position offsets caused by AAOS system-bar/safe-area regions while leaving the existing Normal/OUYA touch path unchanged.

The project uses Android Gradle Plugin 8.6.1 / Gradle 8.7 and requires JDK 17 or newer.

## Controller input regression checks

The release workflow runs the native input checks before building the APK.
They can also run locally with a C++17 compiler, without an Android device or
game data:

```sh
test_binary="$(mktemp)"
trap 'rm -f "$test_binary"' EXIT
c++ -std=c++17 -Wall -Wextra -Werror \
  -Ithird_party/SDL2/include \
  -Ithird_party/ut99dc/Source/NSDLDrv/Inc \
  tools/test_android_controller_input.cpp -o "$test_binary"
"$test_binary"
```

Coverage includes mapped-controller/raw-joystick ownership, overlapping D-pad
sources, menu transitions, held inputs, quick taps, diagonals, input resets,
disconnect/reconnect state, stick and 20% trigger thresholds, and equal
right-stick displacement over time at 20, 30, 60, 90, 120, and 144 FPS.

## Audio regression checks

The release workflow also checks audio pacing without requiring an Android
device or game data:

```sh
python3 tools/test_audio_latency.py
```

The runner uses the production C/C++ mixer and AAudio functions with
deterministic device stubs. It covers fresh-state mixing after queue waits,
single-block queue limits, legacy buffer sizing, pause/resume/shutdown,
fixed-buffer device reuse, queue errors, complete partial writes, bounded
write stalls, and hardware-buffer growth limits. Use `--sanitize` to enable
AddressSanitizer and UndefinedBehaviorSanitizer.

The same runner exercises the real 8/16-bit sound-effect converters and mixer,
including 11,025 Hz to 48 kHz, tiny buffers, fractional downsampling, stereo
frames, shared voice positions and allocation bounds. Use `--resample-only`
to run this focused crash-regression suite.
