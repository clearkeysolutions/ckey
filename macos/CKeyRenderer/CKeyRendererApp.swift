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
    @Published var profile: RenderProfile = .locator { didSet { regenerate() } }
    @Published var payload = "CKey renderer test" { didSet { regenerate() } }
    @Published var foreground = Color(red: 0.95, green: 0.94, blue: 0.88) { didSet { regenerate() } }
    @Published var background = Color.black { didSet { regenerate() } }
    @Published var blur = 0.0 { didSet { regenerate() } }
    @Published var damage = 0.0 { didSet { regenerate() } }
    @Published var noise = 0.0 { didSet { regenerate() } }
    @Published var rotation = 0.0 { didSet { regenerate() } }
    @Published private(set) var svg = ""
    @Published private(set) var status = "Preparing renderer…"

    init() { regenerate() }

    func regenerate() {
        let input = Array(payload.utf8)
        var diag = ckey_diagnostics()
        let rendered: String?
        switch profile {
        case .locator:
            var bytes = [UInt8](repeating: 0, count: Int(CKEY_LOCATOR_PAYLOAD_BYTES))
            for index in bytes.indices { bytes[index] = index < input.count ? input[index] : UInt8(index) }
            var symbol = ckey_symbol()
            guard ckey_encode_locator(bytes, bytes.count, &symbol, &diag) == CKEY_OK else { return fail(diag) }
            rendered = renderLocator(&symbol)
        case .text:
            guard !input.isEmpty, input.count <= Int(CKEY_TEXT_PAYLOAD_MAX_BYTES) else { status = "Profile 2 accepts 1–200 UTF-8 bytes."; return }
            var symbol = ckey_text_symbol()
            guard ckey_encode_text(input, input.count, &symbol, &diag) == CKEY_OK else { return fail(diag) }
            rendered = renderText(&symbol)
        case .extended:
            guard !input.isEmpty, input.count <= Int(CKEY_EXTENDED_PAYLOAD_MAX_BYTES) else { status = "Profile 3 accepts 1–2,048 UTF-8 bytes."; return }
            var symbol = ckey_extended_symbol()
            guard ckey_encode_extended(input, input.count, &symbol, &diag) == CKEY_OK else { return fail(diag) }
            rendered = renderExtended(&symbol)
        }
        guard var next = rendered else { status = "The C++ renderer did not produce SVG."; return }
        next = next.replacingOccurrences(of: "#050505", with: background.hex)
        next = next.replacingOccurrences(of: "#f4f1e8", with: foreground.hex)
        svg = next
        let version = ckey_get_software_version().map { String(cString: $0) } ?? "development"
        status = "\(profile.rawValue) · \(input.count) payload byte(s) · \(version)"
    }

    func saveSVG() {
        let panel = NSSavePanel(); panel.nameFieldStringValue = "ckey-symbol.svg"; panel.allowedContentTypes = [.svg]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        do { try svg.write(to: url, atomically: true, encoding: .utf8); status = "Saved \(url.lastPathComponent)" }
        catch { status = "Could not save SVG: \(error.localizedDescription)" }
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
        guard ckey_render_extended_svg(&symbol, 10, nil, 0, &needed) == CKEY_INSUFFICIENT_BUFFER else { return nil }
        var chars = [CChar](repeating: 0, count: needed)
        let result = chars.withUnsafeMutableBufferPointer { buffer in
            ckey_render_extended_svg(&symbol, 10, buffer.baseAddress, buffer.count, &needed)
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
                TextEditor(text: $model.payload).font(.system(.body, design: .monospaced)).frame(minHeight: 125).overlay(RoundedRectangle(cornerRadius: 8).stroke(.secondary.opacity(0.3)))
                HStack { ColorPicker("Foreground", selection: $model.foreground); ColorPicker("Background", selection: $model.background) }
                LabSlider(title: "Blur", value: $model.blur, range: 0...8, suffix: " px")
                LabSlider(title: "Damage", value: $model.damage, range: 0...45, suffix: " %")
                LabSlider(title: "Noise", value: $model.noise, range: 0...40, suffix: " %")
                LabSlider(title: "Rotation", value: $model.rotation, range: -25...25, suffix: "°")
                HStack { Button("Regenerate") { model.regenerate() }; Button("Save SVG…") { model.saveSVG() } }
                Divider()
                Text(model.status).font(.caption.monospaced()).foregroundStyle(.secondary).textSelection(.enabled)
                Spacer()
            }.padding(22).frame(minWidth: 360, idealWidth: 410)
            SymbolPreview(svg: model.svg, blur: model.blur, damage: model.damage, noise: model.noise, rotation: model.rotation)
                .background(Color(nsColor: .windowBackgroundColor))
        }
    }
}

private struct LabSlider: View {
    let title: String; @Binding var value: Double; let range: ClosedRange<Double>; let suffix: String
    var body: some View { VStack(alignment: .leading) { HStack { Text(title); Spacer(); Text("\(value, specifier: "%.1f")\(suffix)").font(.caption.monospacedDigit()).foregroundStyle(.secondary) }; Slider(value: $value, in: range) } }
}

private struct SymbolPreview: NSViewRepresentable {
    let svg: String; let blur: Double; let damage: Double; let noise: Double; let rotation: Double
    func makeNSView(context: Context) -> WKWebView { WKWebView() }
    func updateNSView(_ webView: WKWebView, context: Context) {
        let html = """
        <html><style>html,body{margin:0;height:100%;display:grid;place-items:center;background:#161616;overflow:hidden}.symbol{position:relative;width:min(84vmin,780px);filter:blur(\(blur)px);transform:rotate(\(rotation)deg)}.symbol svg{width:100%;height:100%;display:block}.damage{position:absolute;inset:0;opacity:\(damage/100);background:repeating-linear-gradient(117deg,transparent 0 19px,#161616 20px 32px),repeating-linear-gradient(27deg,transparent 0 43px,#161616 44px 49px);mix-blend-mode:normal;pointer-events:none}.noise{position:absolute;inset:0;opacity:\(noise/100);background-image:radial-gradient(#fff 0.7px,transparent .9px);background-size:4px 4px;mix-blend-mode:overlay;pointer-events:none}</style><body><div class='symbol'>\(svg)<div class='damage'></div><div class='noise'></div></div></body></html>
        """
        webView.loadHTMLString(html, baseURL: nil)
    }
}

private extension Color {
    var hex: String { let c = NSColor(self).usingColorSpace(.deviceRGB) ?? .white; return String(format: "#%02X%02X%02X", Int(c.redComponent*255), Int(c.greenComponent*255), Int(c.blueComponent*255)) }
}
