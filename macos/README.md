# CKey Renderer

Open `macos/CKeyRenderer.xcodeproj` in Xcode 16 or later, select your Mac as
the run destination, and press Run. The app invokes the portable C++ encoder
and SVG renderer directly; color, blur, rotation, noise, and damage are visual
stress layers applied by the laboratory preview. Those layers are intentionally
not part of the CKey wire format and are useful for generating scanner tests.

The iPhone reader project is `ios/CKeyReader.xcodeproj`. It uses the same C++
core and now attempts Profile 1, then Profile 2, then Profile 3 after a single
candidate localization/refinement pass. Profile 2/3 need adequate pixels per
smallest feature; the lab should be used to establish their real capture limits.
