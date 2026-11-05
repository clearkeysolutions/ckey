// swift-tools-version:5.9
//
// CKey for Swift (iOS 15+, macOS 12+). Add this repository's URL in Xcode
// (File > Add Package Dependencies...) or in Package.swift:
//
//     .package(url: "https://github.com/clearkeysolutions/ckey", from: "0.1.0")
//
// The manifest lives at the repository root because SwiftPM installs a
// package from the root of a Git repository. The Swift code is in sdk/swift;
// see sdk/swift/README.md.
//
// Products
//   CKey         encode, render (CGImage/PNG/SVG/PDF), SwiftUI/UIKit/AppKit
//                views, scanning, payload classification and the optional
//                aggregate telemetry (default mode 1; `CKey.telemetry = .off`).
//   CKeyScanner  scan-only: decoder, camera helper and payload classification.
//                No renderer, no telemetry: nothing of either is compiled
//                into an app that links only this product.
//
// The C++ core is compiled in place from src/ (nothing is copied). SwiftPM
// requires every source of a target to live under that target's directory
// and forbids targets whose directories overlap, so each C++ target is a
// small directory under sdk/swift/Sources/<Target>/ holding a one-line .cpp
// that #includes the matching repository translation unit by a path relative
// to itself (src/ckey_codec.cpp and so on), exactly as CMake builds them.
// CKeyCodec's public include directory carries a module map that exposes the
// repository's include/ckey/ckey.h to Swift, plus a forwarding ckey/ckey.h
// so the core's own `#include "ckey/ckey.h"` resolves without any
// header-search flags (and without unsafeFlags, which SwiftPM refuses in
// dependencies). CKeyTelemetry does the same for include/ckey/telemetry.h.
// The other C++ targets have no headers of their own, but SwiftPM requires
// their include/ directory to exist, so each keeps one with a .gitkeep.
import PackageDescription

let package = Package(
    name: "CKey",
    platforms: [.iOS(.v15), .macOS(.v12)],
    products: [
        .library(name: "CKey", targets: ["CKey"]),
        .library(name: "CKeyScanner", targets: ["CKeyScanKit"]),
    ],
    targets: [
        // C++ core libraries (src/README.md). ckey.h is owned by CKeyCodec.
        .target(name: "CKeyCodec", path: "sdk/swift/Sources/CKeyCodec",
                linkerSettings: [.linkedLibrary("c++")]),
        .target(name: "CKeyScan", dependencies: ["CKeyCodec"], path: "sdk/swift/Sources/CKeyScan",
                linkerSettings: [.linkedLibrary("c++")]),
        .target(name: "CKeyRender", dependencies: ["CKeyCodec"], path: "sdk/swift/Sources/CKeyRender",
                linkerSettings: [.linkedLibrary("c++")]),
        .target(name: "CKeyCertify", dependencies: ["CKeyScan", "CKeyRender"], path: "sdk/swift/Sources/CKeyCertify",
                linkerSettings: [.linkedLibrary("c++")]),
        // Optional telemetry core. It depends on CKeyCodec only for the
        // ckey.h declarations it includes (CKEY_API, ckey_status).
        .target(name: "CKeyTelemetry", dependencies: ["CKeyCodec"], path: "sdk/swift/Sources/CKeyTelemetry",
                linkerSettings: [.linkedLibrary("c++")]),

        // Swift API.
        .target(name: "CKeyScanKit", dependencies: ["CKeyCodec", "CKeyScan"],
                path: "sdk/swift/Sources/CKeyScanKit"),
        .target(name: "CKey",
                dependencies: ["CKeyScanKit", "CKeyCodec", "CKeyRender", "CKeyCertify", "CKeyTelemetry"],
                path: "sdk/swift/Sources/CKey",
                resources: [.copy("PrivacyInfo.xcprivacy")]),

        .testTarget(name: "CKeyScanKitTests", dependencies: ["CKeyScanKit"],
                    path: "sdk/swift/Tests/CKeyScanKitTests"),
        .testTarget(name: "CKeyTests", dependencies: ["CKey"],
                    path: "sdk/swift/Tests/CKeyTests"),
    ],
    cxxLanguageStandard: .cxx20
)
