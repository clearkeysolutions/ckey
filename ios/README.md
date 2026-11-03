# CKey Reader for iPhone

This is an early native camera test app for CKey Profiles 1–3. It tries measured camera geometry against each profile's shared C ABI decoder first, then uses Apple's Vision rectangle detector for rotated/projective fallback localization. Continuous key-based geometry refinement is limited to the strongest candidate on alternating frames; there are no fixed symbol-size ladders. `src/ckey.cpp` is compiled directly into the app so there is no second encoder/decoder implementation. Profile 1 has optical fixtures; Profiles 2 and 3 are experimental camera paths and require physical testing before any reliability claim.

## Run on a phone

1. On a Mac with Xcode, open `ios/CKeyReader.xcodeproj`.
2. Connect and unlock the iPhone, then select it as the run destination.
3. In **Signing & Capabilities**, choose your Apple development team. The bundle identifier is `solutions.clearkey.CKeyReader`; change it if your team requires a unique identifier.
4. Press **Run** and allow camera access when iOS asks.
5. Display a CKey symbol on a separate screen or print it. Keep the whole symbol in view and move closer until the key and module details are clear.

The result is shown as hexadecimal text, with the profile label and payload length. **Detection** timing measures candidate localization; **CKey** timing measures shared-core image decoding. They are per-successful-frame timings, not benchmark statistics.

## Current limits

- All three profile decoders are tried on each candidate; the shared core's profile framing, Reed–Solomon code, and CRC gate successful reads.
- It scans grayscale Y planes from the camera and supports either symbol polarity through the shared core.
- The app has not been compiled or run on an Apple device in the current build environment. The C++ core's image fixture tests do not verify AVFoundation/Vision camera behavior.
- Lighting, distance, blur, rotation, perspective, glare, minimum module size, and device latency have not been characterized. No sub-200 ms or camera-reliability claim is made.
- Full-frame localization and lens-distortion correction remain future work. Profiles 2 and 3 need physical-device tests at distance and under blur, damage, and noise. No end-to-end speed or reliability claim is made.
