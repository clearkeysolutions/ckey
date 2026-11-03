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
