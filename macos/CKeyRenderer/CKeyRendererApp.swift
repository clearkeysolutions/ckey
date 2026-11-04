import AppKit
import ImageIO
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
    case scalable = "Profile 0 · Scalable"
    case locator = "Profile 1 · Locator"
    case text = "Profile 2 · Text"
    case extended = "Profile 3 · Extended"
    var id: String { rawValue }
}

private final class RendererModel: ObservableObject {
    @Published var profile: RenderProfile = .scalable { didSet { scheduleRegenerate() } }
    /// Profile 0: 0 = smallest version that fits; otherwise 1...31.
    @Published var scalableVersion = 0 { didSet { scheduleRegenerate() } }
    /// Profile 0 correction level: 0 L, 1 M, 2 Q, 3 H.
    @Published var scalableLevel = 1 { didSet { scheduleRegenerate() } }
    /// Profile 0 layout: 0 standard, 1 print-robust (P0.1).
    @Published var scalableLayout = 0 { didSet { scheduleRegenerate() } }
    /// Print export width (inches) and printer resolution (dpi).
    @Published var printWidthInches = 1.0
    @Published var printDPI = 600.0
    /// The most recent Profile 0 symbol, kept for print export.
    private let lastSymbol = UnsafeMutablePointer<ckey_scalable_symbol>.allocate(capacity: 1)
    private var hasLastSymbol = false
    /// Module pitch of the current symbol in canonical units (29-unit field).
    @Published private(set) var modulePitch = 1.0
    // Profile 0 design system (ckey_style). Foreground/Background below are
    // the ink and background-centre colours.
    @Published var moduleShape = 0 { didSet { scheduleRegenerate() } }   // rounded, square, circle, diamond
    @Published var ringStyle = 0 { didSet { scheduleRegenerate() } }     // joined arcs, dots
    @Published var rayCap = 0 { didSet { scheduleRegenerate() } }        // round, flat
    @Published var sheen = false { didSet { scheduleRegenerate() } }
    @Published var inkShade = Color(red: 0.784, green: 0.714, blue: 0.604) { didSet { scheduleRegenerate() } }
    @Published var vignette = false { didSet { scheduleRegenerate() } }
    @Published var backgroundEdge = Color(red: 0.027, green: 0.031, blue: 0.047) { didSet { scheduleRegenerate() } }
    @Published var accentsOn = false { didSet { scheduleRegenerate() } }
    @Published var accent1 = Color(red: 0.663, green: 0.722, blue: 0.588) { didSet { scheduleRegenerate() } }
    @Published var accent2 = Color(red: 0.620, green: 0.631, blue: 0.651) { didSet { scheduleRegenerate() } }
    @Published var accentPercent = 18.0 { didSet { scheduleRegenerate() } }
    @Published var bevel = 0.0 { didSet { scheduleRegenerate() } }
    @Published var depth = 0.0 { didSet { scheduleRegenerate() } }
    @Published var grain = 0.0 { didSet { scheduleRegenerate() } }
    @Published private(set) var logoDataURI: String? = nil { didSet { scheduleRegenerate() } }
    @Published private(set) var logoName = ""
    /// 0 = the empty centre; 1 = the largest radius the version allows.
    @Published var logoSize = 0.0 { didSet { scheduleRegenerate() } }
    @Published var logoPlate = true { didSet { scheduleRegenerate() } }
    @Published private(set) var styleStatus = ""
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
    deinit { lastSymbol.deallocate() }

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
        case .scalable:
            let symbol = lastSymbol
            let level = ckey_ecc_level(rawValue: UInt32(scalableLevel))
            let layout = ckey_scalable_layout(rawValue: UInt32(scalableLayout))
            hasLastSymbol = false
            guard ckey_encode_scalable(input, input.count, UInt32(scalableVersion), level, layout, symbol, &diag) == CKEY_OK else { svg = ""; return fail(diag) }
            hasLastSymbol = true
            let version = symbol.pointee.version
            var style = ckey_style()
            ckey_style_default(&style)
            style.background = rgb(background)
            style.background_edge = vignette ? rgb(backgroundEdge) : style.background
            style.ink = rgb(foreground)
            style.ink_shade = sheen ? rgb(inkShade) : style.ink
            if accentsOn {
                style.accents.0 = rgb(accent1); style.accents.1 = rgb(accent2)
                style.accent_count = 2; style.accent_percent = UInt32(accentPercent.rounded())
            }
            style.modules = ckey_module_shape(rawValue: UInt32(moduleShape))
            style.rings = ckey_ring_style(rawValue: UInt32(ringStyle))
            style.rays = ckey_ray_cap(rawValue: UInt32(rayCap))
            style.bevel = bevel; style.shadow = depth; style.texture = grain
            style.logo_plate = logoPlate ? 1 : 0
            let maxRadius = ckey_scalable_logo_max_radius(version, layout)
            style.logo_radius = logoSize <= 0 ? 0 : 3.0 + (maxRadius - 3.0) * logoSize
            var checkDiag = ckey_diagnostics()
            var checked = CKEY_OK
            rendered = (logoDataURI ?? "").withCString { href -> String? in
                style.logo_href = logoDataURI == nil ? nil : href
                checked = ckey_check_scalable_style(symbol, &style, &checkDiag)
                return checked == CKEY_OK ? renderScalable(symbol, style: &style) : nil
            }
            styleStatus = diagnosticString(checkDiag)
            guard checked == CKEY_OK else { svg = ""; status = "Design refused: \(styleStatus)"; return }
            let modules = ckey_scalable_modules_across(version)
            modulePitch = 29.0 / Double(max(modules, 1))
            detail = "\(scalableLayout == 1 ? "P0.1 print · " : "")\(scalableVersion == 0 ? "auto" : "selected") v\(version) · \(modules) modules · level \(["L", "M", "Q", "H"][scalableLevel]) · capacity \(ckey_scalable_capacity(version, level, layout)) B"
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
        if profile != .scalable {  // Profile 0 colours come from its ckey_style
            next = next.replacingOccurrences(of: "#050505", with: background.hex)
            next = next.replacingOccurrences(of: "#f4f1e8", with: foreground.hex)
        }
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
    func chooseLogo() {
        let panel = NSOpenPanel()
        panel.allowedContentTypes = [.png, .jpeg, .svg]
        panel.allowsMultipleSelection = false
        guard panel.runModal() == .OK, let url = panel.url, let data = try? Data(contentsOf: url) else { return }
        guard data.count <= 2_000_000 else { styleStatus = "Logo files must be 2 MB or smaller."; return }
        let ext = url.pathExtension.lowercased()
        let type = ext == "svg" ? "image/svg+xml" : ext == "png" ? "image/png" : "image/jpeg"
        logoName = url.lastPathComponent
        logoDataURI = "data:\(type);base64,\(data.base64EncodedString())"
    }

    func removeLogo() { logoDataURI = nil; logoName = "" }

    /// Exports the current Profile 0 symbol as a 1-bit-style print PNG: black
    /// ink on paper on the printer's dot grid (no grey to halftone), tagged
    /// with its resolution so it prints at the chosen physical width.
    func exportPrintPNG() {
        guard profile == .scalable, hasLastSymbol else { status = "Print export is for Profile 0 symbols."; return }
        var side = 0
        guard ckey_render_scalable_print(lastSymbol, nil, printWidthInches, printDPI, nil, 0, &side) == CKEY_INSUFFICIENT_BUFFER, side > 0 else {
            status = "Print export: width × dpi is out of range."; return
        }
        var pixels = [UInt8](repeating: 255, count: side * side)
        let rendered = pixels.withUnsafeMutableBufferPointer { buffer in
            ckey_render_scalable_print(lastSymbol, nil, printWidthInches, printDPI, buffer.baseAddress, buffer.count, &side)
        }
        guard rendered == CKEY_OK else { status = "Print export failed."; return }
        let panel = NSSavePanel(); panel.nameFieldStringValue = "ckey-print.png"; panel.allowedContentTypes = [.png]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        guard let provider = CGDataProvider(data: Data(pixels) as CFData),
              let image = CGImage(width: side, height: side, bitsPerComponent: 8, bitsPerPixel: 8, bytesPerRow: side,
                                  space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.none.rawValue),
                                  provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent),
              let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
            status = "Print export: could not create the PNG."; return
        }
        let properties = [kCGImagePropertyDPIWidth as String: printDPI, kCGImagePropertyDPIHeight as String: printDPI] as CFDictionary
        CGImageDestinationAddImage(destination, image, properties)
        status = CGImageDestinationFinalize(destination)
            ? "Saved \(url.lastPathComponent): \(String(format: "%.2f", printWidthInches)) in at \(Int(printDPI)) dpi, black on paper. Print at 100% / actual size."
            : "Print export: could not write the PNG."
    }

    /// The original concept art's look: champagne metal, navy vignette,
    /// sage and grey accents, embossed depth, and fine grain.
    func applyConceptPreset() {
        background = Color(red: 0.078, green: 0.086, blue: 0.118); backgroundEdge = Color(red: 0.027, green: 0.031, blue: 0.047)
        foreground = Color(red: 0.965, green: 0.933, blue: 0.878); inkShade = Color(red: 0.784, green: 0.714, blue: 0.604)
        sheen = true; vignette = true; accentsOn = true; accentPercent = 18
        bevel = 0.6; depth = 0.5; grain = 0.4; moduleShape = 0; ringStyle = 0; rayCap = 0
    }

    private func rgb(_ colour: Color) -> ckey_rgb {
        let c = NSColor(colour).usingColorSpace(.deviceRGB) ?? .white
        let byte = { (v: CGFloat) in UInt8(max(0, min(255, (v * 255).rounded()))) }
        return ckey_rgb(r: byte(c.redComponent), g: byte(c.greenComponent), b: byte(c.blueComponent))
    }

    private func diagnosticString(_ diag: ckey_diagnostics) -> String {
        withUnsafeBytes(of: diag.message) { bytes in
            guard let base = bytes.baseAddress else { return "" }
            return String(cString: base.assumingMemoryBound(to: CChar.self))
        }
    }

    private func renderScalable(_ symbol: UnsafeMutablePointer<ckey_scalable_symbol>, style: inout ckey_style) -> String? {
        var needed = 0
        guard ckey_render_scalable_styled_svg(symbol, &style, 24, nil, 0, &needed) == CKEY_INSUFFICIENT_BUFFER else { return nil }
        var chars = [CChar](repeating: 0, count: needed)
        let result = chars.withUnsafeMutableBufferPointer { buffer in
            ckey_render_scalable_styled_svg(symbol, &style, 24, buffer.baseAddress, buffer.count, &needed)
        }
        guard result == CKEY_OK else { return nil }
        return chars.withUnsafeBufferPointer { buffer in
            guard let baseAddress = buffer.baseAddress else { return nil }
            return String(cString: baseAddress)
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
            ScrollView { VStack(alignment: .leading, spacing: 16) {
                Text("CKey Renderer").font(.largeTitle.bold())
                Text("Native C++ encoder · non-normative visual stress laboratory").foregroundStyle(.secondary)
                Picker("Profile", selection: $model.profile) { ForEach(RenderProfile.allCases) { Text($0.rawValue).tag($0) } }.pickerStyle(.segmented)
                if model.profile == .scalable {
                    Picker("Version", selection: $model.scalableVersion) {
                        Text("Auto · smallest that fits").tag(0)
                        ForEach(1...31, id: \.self) { version in
                            Text("v\(version) · \(ckey_scalable_modules_across(UInt32(version))) modules").tag(version)
                        }
                    }
                    Picker("Correction", selection: $model.scalableLevel) {
                        Text("L · ~7%").tag(0); Text("M · ~14%").tag(1); Text("Q · ~22%").tag(2); Text("H · ~30%").tag(3)
                    }.pickerStyle(.segmented)
                    Picker("Layout", selection: $model.scalableLayout) {
                        Text("Standard").tag(0); Text("Print · P0.1").tag(1)
                    }.pickerStyle(.segmented)
                    DisclosureGroup("Print") {
                        VStack(alignment: .leading, spacing: 10) {
                            LabSlider(title: "Width", value: $model.printWidthInches, range: 0.2...4.0, suffix: " in")
                            Picker("Printer", selection: $model.printDPI) {
                                Text("300 dpi").tag(300.0); Text("600 dpi").tag(600.0); Text("1200 dpi").tag(1200.0)
                            }.pickerStyle(.segmented)
                            Button("Export Print PNG…") { model.exportPrintPNG() }
                            Text("Black ink on paper on the printer's dot grid. For small prints use Print · P0.1 and print at 100% / actual size.")
                                .font(.caption).foregroundStyle(.secondary)
                        }.padding(.top, 6)
                    }
                    DisclosureGroup("Design") {
                        VStack(alignment: .leading, spacing: 10) {
                            Picker("Modules", selection: $model.moduleShape) {
                                Text("Rounded").tag(0); Text("Square").tag(1); Text("Circle").tag(2); Text("Diamond").tag(3)
                            }.pickerStyle(.segmented)
                            Picker("Rings", selection: $model.ringStyle) { Text("Joined arcs").tag(0); Text("Dots").tag(1) }.pickerStyle(.segmented)
                            Picker("Rays", selection: $model.rayCap) { Text("Round").tag(0); Text("Flat").tag(1) }.pickerStyle(.segmented)
                            HStack { Toggle("Metal sheen", isOn: $model.sheen); ColorPicker("Shade", selection: $model.inkShade).disabled(!model.sheen) }
                            HStack { Toggle("Vignette", isOn: $model.vignette); ColorPicker("Edge", selection: $model.backgroundEdge).disabled(!model.vignette) }
                            HStack {
                                Toggle("Accents", isOn: $model.accentsOn)
                                ColorPicker("", selection: $model.accent1).disabled(!model.accentsOn)
                                ColorPicker("", selection: $model.accent2).disabled(!model.accentsOn)
                            }
                            LabSlider(title: "Accent share", value: $model.accentPercent, range: 0...60, suffix: " %")
                            LabSlider(title: "Bevel", value: $model.bevel, range: 0...1, suffix: "")
                            LabSlider(title: "Depth", value: $model.depth, range: 0...1, suffix: "")
                            LabSlider(title: "Grain", value: $model.grain, range: 0...1, suffix: "")
                            HStack {
                                Button("Choose Logo…") { model.chooseLogo() }
                                Button("Remove") { model.removeLogo() }.disabled(model.logoName.isEmpty)
                                Text(model.logoName).font(.caption).lineLimit(1)
                            }
                            LabSlider(title: "Logo size", value: $model.logoSize, range: 0...1, suffix: "")
                            Toggle("Logo plate", isOn: $model.logoPlate)
                            Button("Concept preset") { model.applyConceptPreset() }
                            Text(model.styleStatus).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
                        }.padding(.top, 6)
                    }
                }
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
            }.padding(22) }.frame(minWidth: 360, idealWidth: 410)
            SymbolPreview(svg: model.svg, profile: model.profile, modulePitch: model.modulePitch, expectedPayload: Array(model.payload.utf8),
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
    /// Profile 0 module pitch in canonical units; unused by other profiles.
    let modulePitch: Double
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
        let markWidth = profile == .scalable ? 0.68 * modulePitch : profile == .locator ? 0.74 : 0.34
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
            var output = [UInt8](repeating: 0, count: 65535)  // Profile 0 payloads exceed 2,048 bytes
            var scalableVersion: UInt32 = 0
            var scalableLayoutFound: UInt32 = 0
            var outputLength = 0
            var diagnostic = ckey_diagnostics()
            let result: ckey_status = gray.withUnsafeBufferPointer { pixels in
                quad.withUnsafeBufferPointer { q in
                    output.withUnsafeMutableBufferPointer { decoded in
                        switch profile {
                        case .scalable:
                            return ckey_decode_scalable_gray8(pixels.baseAddress, width, height, width, q.baseAddress,
                                                              decoded.baseAddress, decoded.count, &outputLength, &scalableVersion,
                                                              &scalableLayoutFound, &diagnostic)
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
            let density: String
            if profile == .scalable {
                // Profile 0 needs about 6 px per module at every version.
                let pixelsPerModule = pixelsPerUnit * modulePitch
                density = (scalableLayoutFound == 1 ? "P0.1 " : "") + "v\(scalableVersion) · " + String(format: "%.1f px/module", pixelsPerModule) +
                    (pixelsPerModule < 6.0 ? " (below the ~6 px/module decode floor — enlarge the window or raise Resolution)" : "")
            } else {
                let decodeFloor = profile == .locator ? 6.0 : profile == .text ? 10.0 : 11.0
                density = String(format: "%.1f px/unit", pixelsPerUnit) + (pixelsPerUnit < decodeFloor ? " (below the ~\(Int(decodeFloor)) px/unit decode floor — enlarge the window or raise Resolution)" : "")
            }
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
