import CKeyScanKit
import CoreGraphics
import XCTest

/// The pure parts of the camera layer (no camera, no UIKit): the bracket
/// motion, corner utilities, guidance, and the new layout values.
final class CKeyOneEuroFilterTests: XCTestCase {
    func testFirstSampleIsAdopted() {
        var filter = CKeyOneEuroFilter()
        XCTAssertNil(filter.value)
        let first = filter.filter([3, 4], at: 10)
        XCTAssertEqual(first, [3, 4])
        XCTAssertEqual(filter.velocity, [0, 0])
    }

    func testConvergesOnAConstantInput() {
        var filter = CKeyOneEuroFilter(minCutoff: 1.2, beta: 0.02, derivativeCutoff: 6)
        filter.filter([0, 0], at: 0)
        var output = [0.0, 0.0]
        for frame in 1...60 {  // two seconds at 30 fps
            output = filter.filter([100, -40], at: Double(frame) / 30)
        }
        XCTAssertEqual(output[0], 100, accuracy: 0.05)
        XCTAssertEqual(output[1], -40, accuracy: 0.05)
        // Steady once there: the same input moves nothing and the velocity dies out.
        let next = filter.filter([100, -40], at: 61.0 / 30)
        XCTAssertEqual(next[0], output[0], accuracy: 0.01)
        XCTAssertEqual(filter.velocity[0], 0, accuracy: 1)
    }

    func testSmoothsNoiseOnAStillInput() {
        var filter = CKeyOneEuroFilter()
        var low = Double.greatestFiniteMagnitude, high = -Double.greatestFiniteMagnitude
        for frame in 0..<120 {
            // 1.5 pt of alternating measurement noise around 50.
            let output = filter.filter([50 + (frame % 2 == 0 ? 1.5 : -1.5)], at: Double(frame) / 30)
            if frame >= 60 {
                low = min(low, output[0])
                high = max(high, output[0])
            }
        }
        XCTAssertLessThan(high - low, 1.0, "3 pt of peak-to-peak noise should shrink to under 1 pt")
        XCTAssertEqual((high + low) / 2, 50, accuracy: 0.5)
    }

    func testFollowsFastMotionCloserThanSlow() {
        // The cutoff rises with speed: the lag in time is smaller when moving fast.
        func lag(speed: Double) -> Double {
            var filter = CKeyOneEuroFilter()
            var output = 0.0
            for frame in 0..<90 {
                output = filter.filter([speed * Double(frame) / 30], at: Double(frame) / 30)[0]
            }
            return (speed * 89.0 / 30 - output) / speed
        }
        XCTAssertLessThan(lag(speed: 600), lag(speed: 20))
    }

    func testAlpha() {
        XCTAssertEqual(CKeyOneEuroFilter.alpha(cutoff: 1.2, dt: 1.0 / 30), 0.2008, accuracy: 0.001)
        XCTAssertGreaterThan(CKeyOneEuroFilter.alpha(cutoff: 6, dt: 1.0 / 30),
                             CKeyOneEuroFilter.alpha(cutoff: 1.2, dt: 1.0 / 30))
    }
}

final class CKeyCriticalSpringTests: XCTestCase {
    func testReachesTheTargetWithoutOvershoot() {
        var spring = CKeyCriticalSpring(response: 0.08)
        spring.snap(to: [0, 200])
        var peak = -Double.greatestFiniteMagnitude, trough = Double.greatestFiniteMagnitude
        for _ in 0..<60 {  // half a second at 120 Hz
            let value = spring.step(towards: [100, 0], dt: 1.0 / 120)
            peak = max(peak, value[0])
            trough = min(trough, value[1])
        }
        XCTAssertLessThanOrEqual(peak, 100 + 0.01, "critically damped: no bounce past the target")
        XCTAssertGreaterThanOrEqual(trough, -0.01)
        XCTAssertEqual(spring.position?[0] ?? 0, 100, accuracy: 0.01)
        XCTAssertEqual(spring.position?[1] ?? 1, 0, accuracy: 0.01)
        XCTAssertEqual(spring.velocity[0], 0, accuracy: 0.1)
    }

    func testIsMostOfTheWayAfterItsResponseTime() {
        var spring = CKeyCriticalSpring(response: 0.08)
        spring.snap(to: [0])
        var value = 0.0
        for _ in 0..<8 { value = spring.step(towards: [1], dt: 0.01)[0] }  // 80 ms
        XCTAssertGreaterThan(value, 0.9)
        XCTAssertLessThanOrEqual(value, 1.0)
    }

    func testTheStepSizeDoesNotChangeTheResult() {
        // Exact integration: 120 Hz and 60 Hz land in the same place.
        var fine = CKeyCriticalSpring(), coarse = CKeyCriticalSpring()
        fine.snap(to: [0]); coarse.snap(to: [0])
        for _ in 0..<12 { fine.step(towards: [10], dt: 1.0 / 120) }
        for _ in 0..<6 { coarse.step(towards: [10], dt: 1.0 / 60) }
        XCTAssertEqual(fine.position?[0] ?? 0, coarse.position?[0] ?? 1, accuracy: 1e-9)
    }

    func testFirstStepSnaps() {
        var spring = CKeyCriticalSpring()
        let first = spring.step(towards: [5, 6], dt: 0.016)
        XCTAssertEqual(first, [5, 6])
    }
}

final class CKeyBracketMotionTests: XCTestCase {
    private let square = [CGPoint(x: 100, y: 100), CGPoint(x: 200, y: 100),
                          CGPoint(x: 200, y: 200), CGPoint(x: 100, y: 200)]

    func testSnapsOnFirstSightAndHoldsStill() throws {
        var motion = CKeyBracketMotion()
        let first = motion.step(towards: square, capturedAt: 1.00, displayAt: 1.05)
        XCTAssertEqual(first, square)
        var shown = square
        for frame in 1...30 {
            let time = 1.0 + Double(frame) / 30
            let next = motion.step(towards: square, capturedAt: time, displayAt: time + 0.05)
            shown = try XCTUnwrap(next)
        }
        for (a, b) in zip(shown, square) {
            XCTAssertEqual(a.x, b.x, accuracy: 0.001)
            XCTAssertEqual(a.y, b.y, accuracy: 0.001)
        }
    }

    func testNoTargetDrawsNothingAndResets() throws {
        var motion = CKeyBracketMotion()
        _ = motion.step(towards: square, capturedAt: 1, displayAt: 1.05)
        let lost = motion.step(towards: nil, capturedAt: 0, displayAt: 1.06)
        XCTAssertNil(lost)
        // The next sight snaps again, wherever it is.
        let moved = square.map { CGPoint(x: $0.x + 500, y: $0.y) }
        let again = motion.step(towards: moved, capturedAt: 2, displayAt: 2.05)
        XCTAssertEqual(again, moved)
    }

    func testACornerOrderChangeDoesNotTwistTheOutline() throws {
        var motion = CKeyBracketMotion()
        _ = motion.step(towards: square, capturedAt: 1, displayAt: 1.02)
        // The same square reported from another starting corner and winding.
        let reordered = [square[2], square[1], square[0], square[3]]
        let next = motion.step(towards: reordered, capturedAt: 1.033, displayAt: 1.05)
        let shown = try XCTUnwrap(next)
        for (a, b) in zip(shown, square) {
            XCTAssertEqual(a.x, b.x, accuracy: 0.001)
            XCTAssertEqual(a.y, b.y, accuracy: 0.001)
        }
    }

    func testFollowsAMovingCodeWithSmallLag() throws {
        // 300 pt/s to the right, measured at 30 fps with 50 ms latency, drawn at 120 Hz.
        var motion = CKeyBracketMotion()
        var shown = square
        var captured = 0.0
        for tick in 0..<240 {
            let display = Double(tick) / 120
            let frame = floor((display - 0.05) * 30)
            if frame >= 0 { captured = frame / 30 }
            let target = square.map { CGPoint(x: $0.x + 300 * captured, y: $0.y) }
            let next = motion.step(towards: target, capturedAt: captured, displayAt: display)
            shown = try XCTUnwrap(next)
        }
        let truth = 100 + 300 * (239.0 / 120)
        XCTAssertEqual(Double(shown[0].x), truth, accuracy: 40, "the brackets stay within a few tens of points")
        XCTAssertEqual(Double(shown[0].y), 100, accuracy: 0.5)
    }

    func testTunablesReachTheFilterAndSpring() {
        var motion = CKeyBracketMotion()
        motion.minCutoff = 2
        motion.beta = 0.1
        motion.response = 0.2
        motion.leadGain = 0
        XCTAssertEqual(motion.minCutoff, 2)
        XCTAssertEqual(motion.beta, 0.1)
        XCTAssertEqual(motion.response, 0.2)
        XCTAssertEqual(motion.derivativeCutoff, 6)
        XCTAssertEqual(motion.maxLead, 0.12)
    }
}

final class CKeyCornersTests: XCTestCase {
    private let quad = [CGPoint(x: 0, y: 0), CGPoint(x: 10, y: 1), CGPoint(x: 11, y: 12), CGPoint(x: -1, y: 10)]

    func testMatchedFindsEveryCyclicOrder() {
        for reversed in [false, true] {
            let ring = reversed ? Array(quad.reversed()) : quad
            for start in 0..<4 {
                let shuffled = (0..<4).map { ring[(start + $0) % 4] }
                XCTAssertEqual(CKeyCorners.matched(shuffled, to: quad), quad)
            }
        }
        // Anything but two sets of four comes back unchanged.
        XCTAssertEqual(CKeyCorners.matched([.zero], to: quad), [.zero])
    }

    func testCentroidAndBounds() {
        XCTAssertEqual(CKeyCorners.centroid(quad), CGPoint(x: 5, y: 5.75))
        XCTAssertEqual(CKeyCorners.boundingBox(quad), CGRect(x: -1, y: 0, width: 12, height: 12))
        XCTAssertTrue(CKeyCorners.boundingBox([]).isNull)
    }

    func testExpanded() throws {
        let square = [CGPoint(x: 0, y: 0), CGPoint(x: 100, y: 0), CGPoint(x: 100, y: 100), CGPoint(x: 0, y: 100)]
        let grown = try XCTUnwrap(CKeyCorners.expanded(square, padding: 0.10))
        XCTAssertEqual(grown[0].x, -5, accuracy: 1e-9)
        XCTAssertEqual(grown[2].y, 105, accuracy: 1e-9)
        // A tiny code still gets brackets of the minimum size.
        let tiny = square.map { CGPoint(x: $0.x / 10, y: $0.y / 10) }
        let held = try XCTUnwrap(CKeyCorners.expanded(tiny, padding: 0.10, minimumSpan: 32))
        XCTAssertEqual(hypot(held[0].x - 5, held[0].y - 5), 32, accuracy: 1e-9)
        XCTAssertNil(CKeyCorners.expanded([.zero, .zero, .zero, .zero], padding: 0.1))
        XCTAssertNil(CKeyCorners.expanded([.zero], padding: 0.1))
    }

    func testZoomHelpers() {
        let corners = [CGPoint(x: 0.4, y: 0.45), CGPoint(x: 0.6, y: 0.45), CGPoint(x: 0.6, y: 0.55), CGPoint(x: 0.4, y: 0.55)]
        let zoomed = CKeyCorners.scaled(corners, by: 2)
        XCTAssertEqual(zoomed[0].x, 0.3, accuracy: 1e-9)
        XCTAssertEqual(zoomed[2].y, 0.6, accuracy: 1e-9)
        // The widest corner is 0.1 from the centre: 4.7x puts it at the 0.03 inset.
        XCTAssertEqual(CKeyCorners.largestZoomKeepingInFrame(corners, inset: 0.03), 4.7, accuracy: 1e-9)
    }
}

final class CKeyGuidanceTests: XCTestCase {
    func testRawValuesAreTheCoreAdviceFlags() {
        XCTAssertEqual(CKeyGuidance.moveCloser.rawValue, 1)   // CKEY_ADVICE_MOVE_CLOSER
        XCTAssertEqual(CKeyGuidance.moveBack.rawValue, 2)     // CKEY_ADVICE_MOVE_BACK
        XCTAssertEqual(CKeyGuidance.holdSteady.rawValue, 4)   // CKEY_ADVICE_HOLD_STEADY
        XCTAssertEqual(CKeyGuidance.refocus.rawValue, 8)      // CKEY_ADVICE_REFOCUS
        XCTAssertEqual(CKeyGuidance.glare.rawValue, 16)       // CKEY_ADVICE_GLARE
        XCTAssertEqual(CKeyGuidance.all.rawValue, 31)
        // CKEY_ADVICE_REAIM (32) is for the camera, not the user.
        XCTAssertTrue(CKeyGuidance(rawValue: 32).intersection(.all).isEmpty)
    }

    func testPrimaryPicksACutCodeFirst() {
        XCTAssertNil(CKeyGuidance().primary)
        XCTAssertEqual(CKeyGuidance([.moveCloser, .holdSteady]).primary, .holdSteady)
        XCTAssertEqual(CKeyGuidance([.moveCloser, .glare, .moveBack]).primary, .moveBack)
    }

    func testStabilizerNeedsAFewFramesEachWay() {
        var stabilizer = CKeyGuidanceStabilizer(onAfter: 3, offAfter: 6)
        var changes: [CKeyGuidance?] = []
        for _ in 0..<3 { changes.append(stabilizer.update(.moveBack)) }
        XCTAssertEqual(changes, [nil, nil, CKeyGuidance.moveBack], "shown on the third frame in a row")
        // A single frame without the flag does not clear it.
        changes = [stabilizer.update([]), stabilizer.update(.moveBack)]
        XCTAssertEqual(changes, [nil, nil])
        XCTAssertEqual(stabilizer.shown, .moveBack)
        changes = []
        for _ in 0..<6 { changes.append(stabilizer.update([])) }
        XCTAssertEqual(changes, [nil, nil, nil, nil, nil, CKeyGuidance()], "cleared on the sixth frame without it")
        // One odd frame never shows anything.
        changes = [stabilizer.update(.glare), stabilizer.update([])]
        XCTAssertEqual(changes, [nil, nil])
        XCTAssertTrue(stabilizer.shown.isEmpty)
        let hadSomething = stabilizer.reset()
        XCTAssertFalse(hadSomething)
    }
}

final class CKeyTrackFeedTests: XCTestCase {
    func testKeepsTheLatestTrack() {
        let feed = CKeyTrackFeed()
        XCTAssertNil(feed.current)
        let corners = [CGPoint(x: 0.4, y: 0.4), CGPoint(x: 0.6, y: 0.4), CGPoint(x: 0.6, y: 0.6), CGPoint(x: 0.4, y: 0.6)]
        let track = CKeyTrack(corners: corners, captureTime: 12.5, score: 300, decoded: true)
        feed.publish(track)
        XCTAssertEqual(feed.current, track)
        feed.publish(CKeyTrack(corners: [.zero], captureTime: 13, score: 1, decoded: false))
        XCTAssertNil(feed.current, "a track without four corners clears the feed")
        feed.publish(track)
        feed.publish(nil)
        XCTAssertNil(feed.current)
    }
}

final class CKeyScanLayerTests: XCTestCase {
    func testNewLayoutsKeepTheirCoreValues() {
        XCTAssertEqual(CKeyLayout.standard.rawValue, 0)
        XCTAssertEqual(CKeyLayout.print.rawValue, 1)
        XCTAssertEqual(CKeyLayout.standardGrouped.rawValue, 2)   // CKEY_LAYOUT_STANDARD_GROUPED
        XCTAssertEqual(CKeyLayout.printGrouped.rawValue, 3)      // CKEY_LAYOUT_PRINT_GROUPED
        XCTAssertEqual(CKeyLayout.allCases.count, 4)
        XCTAssertTrue(CKeyLayout.printGrouped.isPrint && CKeyLayout.printGrouped.isGrouped)
        XCTAssertTrue(CKeyLayout.standardGrouped.isGrouped && !CKeyLayout.standardGrouped.isPrint)
        XCTAssertFalse(CKeyLayout.standard.isGrouped || CKeyLayout.print.isGrouped)

        let grouped = CKeyResult(bytes: Data("hi".utf8), profile: .scalable, version: 2, layout: .standardGrouped)
        XCTAssertEqual(grouped.telemetryProfileCode, 0)
        XCTAssertEqual(grouped.profileDescription, "Profile 0 · grouped · v2")
        let printGrouped = CKeyResult(bytes: Data("hi".utf8), profile: .scalable, version: 2, layout: .printGrouped)
        XCTAssertEqual(printGrouped.telemetryProfileCode, 1)
        XCTAssertEqual(printGrouped.profileDescription, "Profile 0.1 · print · grouped · v2")
        XCTAssertEqual(CKeyResult(bytes: Data("hi".utf8), profile: .scalable, version: 2, layout: .standard).profileDescription,
                       "Profile 0 · v2")
    }

    func testPixelRegion() throws {
        // The reticle plus 18% on each side, in whole pixels.
        let region = try XCTUnwrap(CKeyDecoder.pixelRegion(normalized: CGRect(x: 0.25, y: 0.25, width: 0.5, height: 0.5),
                                                           width: 1000, height: 800))
        XCTAssertEqual(region, CGRect(x: 160, y: 128, width: 680, height: 544))
        // Clamped to the frame.
        let clamped = try XCTUnwrap(CKeyDecoder.pixelRegion(normalized: CGRect(x: 0, y: 0, width: 1, height: 1),
                                                            width: 640, height: 480))
        XCTAssertEqual(clamped, CGRect(x: 0, y: 0, width: 640, height: 480))
        // Too small to hold a code.
        XCTAssertNil(CKeyDecoder.pixelRegion(normalized: CGRect(x: 0.5, y: 0.5, width: 0.01, height: 0.01),
                                             width: 640, height: 480))
        XCTAssertNil(CKeyDecoder.pixelRegion(normalized: .null, width: 640, height: 480))
    }

    func testScanOfABlankFrameFindsNothing() {
        let white = [UInt8](repeating: 255, count: 320 * 240)
        white.withUnsafeBufferPointer { buffer in
            let whole = CKeyDecoder.scan(gray: buffer.baseAddress!, width: 320, height: 240, stride: 320)
            XCTAssertNil(whole.result)
            XCTAssertFalse(whole.located)
            XCTAssertTrue(whole.quad.isEmpty && whole.normalizedQuad.isEmpty)
            XCTAssertEqual(whole.frameSize, CGSize(width: 320, height: 240))
            XCTAssertEqual(whole.erasuresUsed, 0)
            let region = CKeyDecoder.scan(gray: buffer.baseAddress!, width: 320, height: 240, stride: 320,
                                          region: CGRect(x: 80, y: 60, width: 160, height: 120))
            XCTAssertNil(region.result)
            XCTAssertNil(CKeyDecoder.decode(gray: buffer.baseAddress!, width: 320, height: 240, stride: 320,
                                            region: CGRect(x: 80, y: 60, width: 160, height: 120)))
        }
    }

    func testFrameProcessorOnABlankFrame() {
        let processor = CKeyFrameProcessor()
        XCTAssertTrue(processor.regionOfInterest)
        XCTAssertTrue(processor.tracking)
        XCTAssertEqual(processor.scanRegion, CKeyFrameProcessor.defaultScanRegion)
        processor.scanRegion = CGRect(x: 0.3, y: 0.3, width: 0.001, height: 0.001)
        XCTAssertNil(processor.scanRegion, "a region too small to hold a code means whole frames")
        var advised = 0
        processor.onAdvice = { advice in
            advised += 1
            XCTAssertFalse(advice.hasCandidate)
            XCTAssertEqual(advice.zoomFactor, 1)
        }
        let gray = [UInt8](repeating: 128, count: 320 * 240)
        gray.withUnsafeBufferPointer { buffer in
            for frame in 0..<4 {
                let outcome = processor.process(gray: buffer.baseAddress!, width: 320, height: 240, stride: 320,
                                                captureTime: Double(frame) / 30)
                XCTAssertNotNil(outcome)
                XCTAssertNil(outcome?.result)
            }
        }
        XCTAssertEqual(advised, 4, "advice comes once per analysed frame")
    }
}
