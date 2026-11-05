# CKey Renderer

Open `macos/CKeyRenderer.xcodeproj` in Xcode 16 or later, select your Mac as
the run destination, and press Run. The app invokes the portable C++ encoder
and SVG renderer directly. Its preview can apply color, blur, rotation, damage,
noise, and raster downsampling, then sends the displayed pixels through the same
projective C++ decoder used by the phone app. The pass/fail indicator tests a
known crop, not full-frame localization. Effects are non-normative and do not
change the wire format.

The iPhone reader project is `ios/CKeyReader.xcodeproj`. It uses the same C++
core and now attempts Profile 1, then Profile 2, then Profile 3 after a single
candidate localization/refinement pass. Profile 2/3 need adequate pixels per
smallest feature; the lab should be used to establish their real capture limits.

## Verify the build has the current core

The status line ends with the compiled core version, which must be **`0.1.0-build.61`**. If it shows an older version, run **Product → Clean Build Folder** (⇧⌘K) and rebuild.

With current code you should see:

- **P2 and P3 previews have no empty band.** The square grid is evenly filled, every P3 ray around the dial varies, and there is a clear moat around each finder, including on its inner side.
- **Profile 0 is the default.** Its version picker defaults to Auto (the smallest version that fits), and Correction sets L/M/Q/H. The read check reports the detected version and `px/module`; P0 needs about 6 px/module.
- **The preview read check shows `PASS` for all profiles** with effects at zero. The check reports the raster density it decoded at (`px/unit`).
- **The P3 read check depends on window size.** P3 needs about 11 px/unit, and the preview is at most 780 points wide (about 12.6 px/unit on a Retina display at full size). A `FAIL` that is flagged as below the decode floor reflects the window or the Resolution slider, not the symbol.
