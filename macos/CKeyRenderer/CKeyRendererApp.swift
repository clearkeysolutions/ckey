import AppKit
import SwiftUI
import UniformTypeIdentifiers
import WebKit

@main
struct CKeyRendererApp: App {
    var body: some Scene {
        WindowGroup("CKey Renderer") {
            RendererView()
                .frame(minWidth: 980, minHeight: 700)
        }
    }
}

private enum RenderProfile: String, CaseIterable, Identifiable {
    case locator = "Profile 1 · Locator"
    case text = "Profile 2 · Text"
    case extended = "Profile 3 · Extended"
    var id: String { rawValue }
}

private final class RendererModel: ObservableObject {
    @Published var profile: RenderProfile = .locator { didSet { scheduleRegenerate() } }
    @Published var payload = "CKeyDemoPayload!" { didSet { scheduleRegenerate() } }
    @Published var extendedTier = 0 { didSet { scheduleRegenerate() } }
    @Published var foreground = Color(red: 0.95, green: 0.94, blue: 0.88) { didSet { scheduleRegenerate() } }
    @Published var background = Color.black { didSet { scheduleRegenerate() } }
    @Published var blur = 0.0
    @Published var damage = 0.0
    @Published var noise = 0.0
    @Published var rotation = 0.0
    @Published var resolution = 1.0
    @Published private(set) var svg = ""
    @Published private(set) var status = "Preparing renderer…"
    @Published private(set) var previewReadStatus = "Synthetic preview: waiting for a symbol."

    private var regenerationWorkItem: DispatchWorkItem?

    init() { scheduleRegenerate() }

    private func scheduleRegenerate() {
        regenerationWorkItem?.cancel()
        let work = DispatchWorkItem { [weak self] in self?.regenerate() }
        regenerationWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.12, execute: work)
    }

    func regenerate() {
        let input = Array(payload.utf8)
        var diag = ckey_diagnostics()
        let rendered: String?
        var detail = ""
        switch profile {
        case .locator:
            guard input.count == Int(CKEY_LOCATOR_PAYLOAD_BYTES) else {
                svg = ""; status = "Profile 1 requires exactly 16 UTF-8 bytes; entered \(input.count)."; return
            }
            let bytes = input
            var symbol = ckey_symbol()
            guard ckey_encode_locator(bytes, bytes.count, &symbol, &diag) == CKEY_OK else { svg = ""; return fail(diag) }
            rendered = renderLocator(&symbol)
            detail = "RS(77,26), exactly 16 bytes"
        case .text:
            guard !input.isEmpty, input.count <= Int(CKEY_TEXT_PAYLOAD_MAX_BYTES) else { svg = ""; status = "Profile 2 accepts 1–200 UTF-8 bytes."; return }
            var symbol = ckey_text_symbol()
            guard ckey_encode_text(input, input.count, &symbol, &diag) == CKEY_OK else { svg = ""; return fail(diag) }
            rendered = renderText(&symbol)
            detail = "RS(255,210), 200-byte maximum"
        case .extended:
            guard !input.isEmpty, input.count <= Int(CKEY_EXTENDED_PAYLOAD_MAX_BYTES) else { svg = ""; status = "Profile 3 accepts 1–2,048 UTF-8 bytes."; return }
            var symbol = ckey_extended_symbol()
            let encodeStatus: ckey_status
            switch extendedTier {
            case 256: encodeStatus = ckey_encode_extended_tiered(input, input.count, CKEY_EXTENDED_TIER_256, &symbol, &diag)
            case 384: encodeStatus = ckey_encode_extended_tiered(input, input.count, CKEY_EXTENDED_TIER_384, &symbol, &diag)
            case 512: encodeStatus = ckey_encode_extended_tiered(input, input.count, CKEY_EXTENDED_TIER_512, &symbol, &diag)
            case 768: encodeStatus = ckey_encode_extended_tiered(input, input.count, CKEY_EXTENDED_TIER_768, &symbol, &diag)
            case 1024: encodeStatus = ckey_encode_extended_tiered(input, input.count, CKEY_EXTENDED_TIER_1024, &symbol, &diag)
            case 1536: encodeStatus = ckey_encode_extended_tiered(input, input.count, CKEY_EXTENDED_TIER_1536, &symbol, &diag)
            case 2048: encodeStatus = ckey_encode_extended_tiered(input, input.count, CKEY_EXTENDED_TIER_2048, &symbol, &diag)
            default: encodeStatus = ckey_encode_extended(input, input.count, &symbol, &diag)
            }
            guard encodeStatus == CKEY_OK else { svg = ""; return fail(diag) }
            rendered = renderExtended(&symbol)
            let tier = extendedTier == 0 ? ([256, 384, 512, 768, 1024, 1536, 2048].first { input.count <= $0 } ?? 2048) : extendedTier
            let dataBytes = (11 + tier + 9) / 10
            detail = "\(extendedTier == 0 ? "auto" : "selected") tier \(tier) B · RS(255,\(dataBytes)) × 10"
        }
        guard var next = rendered else { svg = ""; status = "The C++ renderer did not produce SVG."; return }
        next = next.replacingOccurrences(of: "#050505", with: background.hex)
        next = next.replacingOccurrences(of: "#f4f1e8", with: foreground.hex)
        svg = next
        let version = ckey_get_software_version().map { String(cString: $0) } ?? "development"
        status = "\(profile.rawValue) · \(input.count) payload byte(s) · \(detail) · \(version)"
    }

    func saveSVG() {
        let panel = NSSavePanel(); panel.nameFieldStringValue = "ckey-symbol.svg"; panel.allowedContentTypes = [.svg]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do { try svg.write(to: url, atomically: true, encoding: .utf8); status = "Saved \(url.lastPathComponent)" }
        catch { status = "Could not save SVG: \(error.localizedDescription)" }
    }

    func recordPreviewRead(_ result: String) {
        if previewReadStatus != result { previewReadStatus = result }
    }

    private func fail(_ diag: ckey_diagnostics) {
        // C fixed-size arrays import to Swift as tuples, not C string pointers.
        // Reinterpret only the message field's storage when constructing String.
        status = withUnsafeBytes(of: diag.message) { bytes in
            guard let baseAddress = bytes.baseAddress else { return "CKey operation failed." }
            return String(cString: baseAddress.assumingMemoryBound(to: CChar.self))
        }
    }
    private func renderLocator(_ symbol: inout ckey_symbol) -> String? {
        var needed = 0
        guard ckey_render_svg(&symbol, 24, nil, 0, &needed) == CKEY_INSUFFICIENT_BUFFER else { return nil }
        var chars = [CChar](repeating: 0, count: needed)
        let result = chars.withUnsafeMutableBufferPointer { buffer in
            ckey_render_svg(&symbol, 24, buffer.baseAddress, buffer.count, &needed)
        }
        guard result == CKEY_OK else { return nil }
        return chars.withUnsafeBufferPointer { buffer in
            guard let baseAddress = buffer.baseAddress else { return nil }
            return String(cString: baseAddress)
        }
    }
    private func renderText(_ symbol: inout ckey_text_symbol) -> String? {
        var needed = 0
        guard ckey_render_text_svg(&symbol, 24, nil, 0, &needed) == CKEY_INSUFFICIENT_BUFFER else { return nil }
        var chars = [CChar](repeating: 0, count: needed)
        let result = chars.withUnsafeMutableBufferPointer { buffer in
            ckey_render_text_svg(&symbol, 24, buffer.baseAddress, buffer.count, &needed)
        }
        guard result == CKEY_OK else { return nil }
        return chars.withUnsafeBufferPointer { buffer in
            guard let baseAddress = buffer.baseAddress else { return nil }
            return String(cString: baseAddress)
        }
    }
    private func renderExtended(_ symbol: inout ckey_extended_symbol) -> String? {
        var needed = 0
        guard ckey_render_extended_svg(&symbol, 24, nil, 0, &needed) == CKEY_INSUFFICIENT_BUFFER else { return nil }
        var chars = [CChar](repeating: 0, count: needed)
        let result = chars.withUnsafeMutableBufferPointer { buffer in
            ckey_render_extended_svg(&symbol, 24, buffer.baseAddress, buffer.count, &needed)
        }
        guard result == CKEY_OK else { return nil }
        return chars.withUnsafeBufferPointer { buffer in
            guard let baseAddress = buffer.baseAddress else { return nil }
            return String(cString: baseAddress)
        }
    }
}

private struct RendererView: View {
    @StateObject private var model = RendererModel()
    var body: some View {
        HSplitView {
            VStack(alignment: .leading, spacing: 16) {
                Text("CKey Renderer").font(.largeTitle.bold())
                Text("Native C++ encoder · non-normative visual stress laboratory").foregroundStyle(.secondary)
                Picker("Profile", selection: $model.profile) { ForEach(RenderProfile.allCases) { Text($0.rawValue).tag($0) } }.pickerStyle(.segmented)
                if model.profile == .extended {
                    Picker("Capacity / ECC", selection: $model.extendedTier) {
                        Text("Auto · strongest that fits").tag(0)
                        ForEach([256, 384, 512, 768, 1024, 1536, 2048], id: \.self) { capacity in
                            Text("\(capacity) B · RS tier").tag(capacity)
                        }
                    }
                }
                TextEditor(text: $model.payload).font(.system(.body, design: .monospaced)).frame(minHeight: 125).overlay(RoundedRectangle(cornerRadius: 8).stroke(.secondary.opacity(0.3)))
                HStack { ColorPicker("Foreground", selection: $model.foreground); ColorPicker("Background", selection: $model.background) }
                LabSlider(title: "Blur", value: $model.blur, range: 0...2.5, suffix: " × module")
                LabSlider(title: "Damage", value: $model.damage, range: 0...45, suffix: " %")
                LabSlider(title: "Noise", value: $model.noise, range: 0...40, suffix: " %")
                LabSlider(title: "Rotation", value: $model.rotation, range: -25...25, suffix: "°")
                LabSlider(title: "Resolution", value: $model.resolution, range: 0.1...1.0, suffix: "×")
                HStack { Button("Regenerate") { model.regenerate() }; Button("Save SVG…") { model.saveSVG() }.disabled(model.svg.isEmpty) }
                Divider()
                Text(model.status).font(.caption.monospaced()).foregroundStyle(.secondary).textSelection(.enabled)
                Text(model.previewReadStatus).font(.caption.monospaced()).foregroundStyle(model.previewReadStatus.contains("PASS") ? .green : .secondary).textSelection(.enabled)
                Text("The read check uses the displayed raster and the same native decoder as CKeyReader.").font(.caption).foregroundStyle(.secondary)
                Spacer()
            }.padding(22).frame(minWidth: 360, idealWidth: 410)
            SymbolPreview(svg: model.svg, profile: model.profile, expectedPayload: Array(model.payload.utf8),
                          backgroundHex: model.background.hex, blur: model.blur, damage: model.damage, noise: model.noise,
                          rotation: model.rotation, resolution: model.resolution,
                          onRead: model.recordPreviewRead)
                .background(Color(nsColor: .windowBackgroundColor))
        }
    }
}

private struct LabSlider: View {
    let title: String; @Binding var value: Double; let range: ClosedRange<Double>; let suffix: String
    var body: some View { VStack(alignment: .leading) { HStack { Text(title); Spacer(); Text("\(value, specifier: "%.1f")\(suffix)").font(.caption.monospacedDigit()).foregroundStyle(.secondary) }; Slider(value: $value, in: range) } }
}

private struct SymbolPreview: NSViewRepresentable {
    let svg: String
    let profile: RenderProfile
    let expectedPayload: [UInt8]
    let backgroundHex: String
    let blur: Double; let damage: Double; let noise: Double; let rotation: Double
    let resolution: Double
    let onRead: (String) -> Void
    final class Coordinator: NSObject, WKNavigationDelegate {
        var lastSVG: String?
        var readWorkItem: DispatchWorkItem?
        var onNavigationFinished: (() -> Void)?
        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
            onNavigationFinished?()
        }
    }
    func makeCoordinator() -> Coordinator { Coordinator() }
    func makeNSView(context: Context) -> WKWebView {
        let webView = WKWebView()
        webView.navigationDelegate = context.coordinator
        return webView
    }
    func updateNSView(_ webView: WKWebView, context: Context) {
        let coordinator = context.coordinator
        coordinator.onNavigationFinished = { [weak webView, weak coordinator] in
            guard let webView, let coordinator else { return }
            self.scheduleRead(webView, coordinator: coordinator, delay: 0.08)
        }
        if coordinator.lastSVG != svg {
            coordinator.lastSVG = svg
            let moduleSize = moduleSizeInCSSPixels(webView)
            let html = """
            <html><style>html,body{margin:0;width:100%;height:100%;display:grid;place-items:center;background:#161616;overflow:hidden}.symbol{position:relative;width:min(84vmin,780px);aspect-ratio:1}.symbol svg{width:100%;height:100%;display:block}.damage,.noise{position:absolute;inset:0;pointer-events:none;opacity:0}.noise{mix-blend-mode:overlay}</style><body><div class='symbol'>\(svg)<div class='damage' id='damage'></div><div class='noise' id='noise'></div></div><script>window.setEffects=function(b,d,n,r,m,bg){let s=document.querySelector('.symbol'),damage=document.getElementById('damage'),noise=document.getElementById('noise');s.style.filter='blur('+(b*m)+'px)';s.style.transform='rotate('+r+'deg)';damage.style.opacity=d;damage.style.backgroundImage='repeating-linear-gradient(117deg,transparent 0 '+(m*8)+'px,'+bg+' '+(m*8)+'px '+(m*10)+'px),repeating-linear-gradient(27deg,transparent 0 '+(m*17)+'px,'+bg+' '+(m*17)+'px '+(m*19)+'px)';noise.style.opacity=n;noise.style.backgroundImage='radial-gradient(#fff 0.7px,transparent 0.9px)';noise.style.backgroundSize=(m*1.6)+'px '+(m*1.6)+'px'};window.setEffects(\(blur),\(damage / 100),\(noise / 100),\(rotation),\(moduleSize),'\(backgroundHex)');</script></body></html>
            """
            webView.loadHTMLString(html, baseURL: nil)
        } else {
            let script = "window.setEffects(\(blur),\(damage / 100),\(noise / 100),\(rotation),\(moduleSizeInCSSPixels(webView)),'\(backgroundHex)')"
            webView.evaluateJavaScript(script) { [weak webView] _, _ in
                guard let webView else { return }
                self.scheduleRead(webView, coordinator: coordinator, delay: 0.12)
            }
        }
    }

    private func moduleSizeInCSSPixels(_ webView: WKWebView) -> Double {
        let side = min(min(Double(webView.bounds.width), Double(webView.bounds.height)) * 0.84, 780.0)
        let viewBoxSide = profile == .extended ? 124.0 : 31.0
        let markWidth = profile == .locator ? 0.74 : 0.34
        return side / viewBoxSide * markWidth
    }

    private func scheduleRead(_ webView: WKWebView, coordinator: Coordinator, delay: Double) {
        coordinator.readWorkItem?.cancel()
        let work = DispatchWorkItem { [weak webView] in
            guard let webView else { return }
            self.readDisplayedRaster(webView)
        }
        coordinator.readWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }

    private func readDisplayedRaster(_ webView: WKWebView) {
        guard !svg.isEmpty, webView.bounds.width > 0, webView.bounds.height > 0 else {
            onRead("Synthetic preview: no valid symbol to decode."); return
        }
        webView.takeSnapshot(with: nil) { image, error in
            guard error == nil, let image,
                  let cgImage = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else {
                onRead("Synthetic preview: could not capture the displayed raster."); return
            }
            let width = max(2, Int((Double(cgImage.width) * resolution).rounded()))
            let height = max(2, Int((Double(cgImage.height) * resolution).rounded()))
            guard width > 1, height > 1 else { onRead("Synthetic preview: raster is too small."); return }
            var gray = [UInt8](repeating: 0, count: width * height)
            let rendered = gray.withUnsafeMutableBytes { bytes -> Bool in
                guard let context = CGContext(data: bytes.baseAddress, width: width, height: height,
                                              bitsPerComponent: 8, bytesPerRow: width,
                                              space: CGColorSpaceCreateDeviceGray(),
                                              bitmapInfo: CGImageAlphaInfo.none.rawValue) else { return false }
                context.interpolationQuality = .high
                context.translateBy(x: 0, y: CGFloat(height)); context.scaleBy(x: 1, y: -1)
                context.draw(cgImage, in: CGRect(x: 0, y: 0, width: width, height: height))
                return true
            }
            guard rendered else { onRead("Synthetic preview: grayscale conversion failed."); return }

            let side = min(min(Double(webView.bounds.width), Double(webView.bounds.height)) * 0.84, 780.0)
            let left = (Double(webView.bounds.width) - side) / 2
            let top = (Double(webView.bounds.height) - side) / 2
            let viewBoxSide = profile == .extended ? 124.0 : 31.0
            let fieldSide = profile == .extended ? 116.0 : 29.0
            let inset = profile == .extended ? 4.0 : 1.0
            let moduleScale = side / viewBoxSide
            let sx = Double(width) / Double(webView.bounds.width)
            let sy = Double(height) / Double(webView.bounds.height)
            let cx = left + side / 2, cy = top + side / 2
            let angle = rotation * Double.pi / 180
            var quad: [Double] = []
            for (u, v) in [(0.0, 0.0), (fieldSide, 0.0), (fieldSide, fieldSide), (0.0, fieldSide)] {
                let x = left + (inset + u) * moduleScale
                let y = top + (inset + v) * moduleScale
                let dx = x - cx, dy = y - cy
                quad.append((cx + dx * cos(angle) - dy * sin(angle)) * sx)
                quad.append((cy + dx * sin(angle) + dy * cos(angle)) * sy)
            }
            var output = [UInt8](repeating: 0, count: Int(CKEY_EXTENDED_PAYLOAD_MAX_BYTES))
            var outputLength = 0
            var diagnostic = ckey_diagnostics()
            let result: ckey_status = gray.withUnsafeBufferPointer { pixels in
                quad.withUnsafeBufferPointer { q in
                    output.withUnsafeMutableBufferPointer { decoded in
                        switch profile {
                        case .locator:
                            return ckey_decode_gray8(pixels.baseAddress, width, height, width, q.baseAddress,
                                                     decoded.baseAddress, Int(CKEY_LOCATOR_PAYLOAD_BYTES), &diagnostic)
                        case .text:
                            return ckey_decode_text_gray8(pixels.baseAddress, width, height, width, q.baseAddress,
                                                          decoded.baseAddress, decoded.count, &outputLength, &diagnostic)
                        case .extended:
                            return ckey_decode_extended_gray8(pixels.baseAddress, width, height, width, q.baseAddress,
                                                              decoded.baseAddress, decoded.count, &outputLength, &diagnostic)
                        }
                    }
                }
            }
            let decodedCount = profile == .locator ? Int(CKEY_LOCATOR_PAYLOAD_BYTES) : outputLength
            // Raster pixels per symbol unit actually presented to the decoder.
            // Clean-render floors are about 6 (P1), 10 (P2), and 11 (P3);
            // below them a FAIL reflects the window/resolution, not the symbol.
            let pixelsPerUnit = moduleScale * sx
            let decodeFloor = profile == .locator ? 6.0 : profile == .text ? 10.0 : 11.0
            let density = String(format: "%.1f px/unit", pixelsPerUnit) + (pixelsPerUnit < decodeFloor ? " (below the ~\(Int(decodeFloor)) px/unit decode floor — enlarge the window or raise Resolution)" : "")
            let expected = expectedPayload
            let matches = result == CKEY_OK && decodedCount == expected.count &&
                          Array(output.prefix(decodedCount)) == expected
            if matches {
                onRead("Synthetic preview decode: PASS · \(decodedCount) bytes · \(density)")
            } else {
                let message = withUnsafeBytes(of: diagnostic.message) { bytes -> String in
                    guard let address = bytes.baseAddress else { return "decode rejected" }
                    return String(cString: address.assumingMemoryBound(to: CChar.self))
                }
                onRead("Synthetic preview decode: FAIL · \(message) · \(density)")
            }
        }
    }
}

private extension Color {
    var hex: String { let c = NSColor(self).usingColorSpace(.deviceRGB) ?? .white; return String(format: "#%02X%02X%02X", Int(c.redComponent*255), Int(c.greenComponent*255), Int(c.blueComponent*255)) }
}
