# Ready_To_Go — compiled artifacts

Two pre-built binaries, ready to install:

- `rf_logger.fap` — the Flipper Zero app (copy to `/ext/apps/Sub-GHz/` on the SD card)
- `RF_Triangulator.apk` — the Android app (install with `adb install` or sideload)

These are built from the sources in `../Build/`. If you want to rebuild them yourself:

## Build `rf_logger.fap`

```bash
# inside a clone of the Flipper Zero firmware repo
cp -r ../Mhz_Localise/Build/rf_logger applications_user/
./fbt fap_rf_logger
cp build/f7-firmware-D/.extapps/rf_logger.fap ../Mhz_Localise/Ready_To_Go/
```

Requires the official or Unleashed firmware (≥ 0.97) and `fbt`.

## Build `RF_Triangulator.apk`

The Android app is a Capacitor wrapper around the web UI in `Build/android_app/www/` plus the native plugin in `Build/android_app/plugin/`. To assemble a full Android Studio project you need to:

1. Create a Capacitor project (`npm init @capacitor/app`).
2. Copy `Build/android_app/www/*` into the project's `www/` directory — including `spectrum.csv` which powers the Allocation List tab.
3. Copy `Build/android_app/plugin/FlipperSerialPlugin.java` into `android/app/src/main/java/com/mhzlocalise/rftriangulator/`.
4. Add `usb-serial-for-android` to `android/app/build.gradle`:
   ```
   implementation 'com.github.mik3y:usb-serial-for-android:3.7.0'
   ```
5. Register the plugin in `MainActivity.java`:
   ```java
   registerPlugin(FlipperSerialPlugin.class);
   ```
6. Add USB host feature + intent filter to `AndroidManifest.xml` (standard Capacitor USB serial setup).
7. Build:
   ```bash
   npx cap sync android
   cd android && ./gradlew assembleDebug
   cp app/build/outputs/apk/debug/app-debug.apk ../../Ready_To_Go/RF_Triangulator.apk
   ```

This step-by-step bootstrap is intentionally not automated — it depends on your local Capacitor / Android SDK versions.
