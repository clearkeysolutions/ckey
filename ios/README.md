# CKey Reader for iPhone

This is an early native camera test app for CKey Profiles 1–3. It uses Apple's Vision rectangle detector for candidate localization and calls the existing C ABI for calibration, projective sampling, Reed–Solomon correction, and CRC validation. `src/ckey.cpp` is compiled directly into the app so there is no second encoder/decoder implementation. Profile 1 has an optical fixture; Profiles 2 and 3 are new high-resolution experimental paths and require physical testing before any reliability claim.

## Run on a phone

1. On a Mac with Xcode, open `ios/CKeyReader.xcodeproj`.
2. Connect and unlock the iPhone, then select it as the run destination.
3. In **Signing & Capabilities**, choose your Apple development team. The bundle identifier is `solutions.clearkey.CKeyReader`; change it if your team requires a unique identifier.
4. Press **Run** and allow camera access when iOS asks.
5. Display a CKey symbol on a separate screen or print it. Keep the whole symbol in view and move closer until the key and module details are clear.

The result is shown as 32 hexadecimal digits. **Vision** timing measures rectangle localization; **CKey** timing measures the image-to-payload core after localization. They are per-successful-frame timings, not benchmark statistics.

## Current limits

- The reader tests Vision rectangle candidates and relies on the CKey keys, Reed–Solomon code, and CRC to reject false candidates.
- It scans grayscale Y planes from the camera and supports either symbol polarity through the shared core.
- The app has not been compiled or run on an Apple device in the current build environment. The C++ core's image fixture tests do not verify AVFoundation/Vision camera behavior.
- Lighting, distance, blur, rotation, perspective, glare, minimum module size, and device latency have not been characterized. No sub-200 ms or camera-reliability claim is made.
- Full-frame localization, lens-distortion correction, continuous scanning benchmarks, and multi-version reading are future work.
