import SwiftUI

@main
struct CKeyReaderApp: App {
    var body: some Scene {
        WindowGroup {
            ReaderView()
                .preferredColorScheme(.dark)
        }
    }
}

struct ReaderView: View {
    @StateObject private var scanner = CameraScanner()
    private var softwareVersion: String {
        guard let version = ckey_get_software_version() else { return "development build" }
        return String(cString: version)
    }

    var body: some View {
        VStack(spacing: 18) {
            HStack {
                VStack(alignment: .leading, spacing: 4) {
                    Text("CKey Reader").font(.largeTitle.bold())
                    Text("Profiles 1–3 · \(softwareVersion)")
                        .font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
                Circle().fill(scanner.isRunning ? Color.green : Color.orange).frame(width: 10, height: 10)
            }

            ZStack {
                CameraPreview(session: scanner.session)
                RoundedRectangle(cornerRadius: 28)
                    .stroke(.white.opacity(0.8), lineWidth: 2)
                    .padding(38)
                    .allowsHitTesting(false)
                VStack {
                    Spacer()
                    Text("Fit the full CKey inside the frame")
                        .font(.footnote.weight(.medium))
                        .padding(.horizontal, 14).padding(.vertical, 8)
                        .background(.black.opacity(0.65), in: Capsule())
                        .padding(.bottom, 54)
                }
            }
            .aspectRatio(0.78, contentMode: .fit)
            .clipShape(RoundedRectangle(cornerRadius: 28))
            .overlay(RoundedRectangle(cornerRadius: 28).stroke(.white.opacity(0.12)))

            VStack(alignment: .leading, spacing: 8) {
                Label(scanner.status, systemImage: scanner.decodedHex == nil ? "viewfinder" : "checkmark.circle.fill")
                    .font(.headline)
                    .foregroundColor(scanner.decodedHex == nil ? Color.primary : Color.green)
                if let hex = scanner.decodedHex {
                    Text(scanner.decodedProfile).font(.caption.weight(.semibold)).foregroundStyle(.green)
                    if let text = scanner.decodedText {
                        Text(text).font(.body.weight(.medium)).textSelection(.enabled)
                            .padding(12).frame(maxWidth: .infinity, alignment: .leading)
                            .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
                        if let action = scanner.decodedAction {
                            PayloadActionButton(action: action)
                                .font(.callout.weight(.semibold))
                                .lineLimit(2)
                        }
                        Text("\(text.utf8.count) bytes · hex \(hex)")
                            .font(.caption2.monospaced()).foregroundStyle(.secondary).textSelection(.enabled)
                            .lineLimit(2).truncationMode(.middle)
                    } else {
                        Text(hex).font(.system(.callout, design: .monospaced)).textSelection(.enabled)
                            .padding(12).frame(maxWidth: .infinity, alignment: .leading)
                            .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
                        Text("Binary payload · \(hex.count / 2) bytes").font(.caption2).foregroundStyle(.secondary)
                    }
                    Text("Detection: \(String(format: "%.1f", scanner.detectionMilliseconds)) ms · CKey: \(String(format: "%.1f", scanner.decodeMilliseconds)) ms")
                        .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                } else {
                    Text("The phone finds the symbol, then the C++ core checks its keys, error correction, and checksum.")
                        .font(.subheadline).foregroundStyle(.secondary)
                    Text("Detection: \(String(format: "%.1f", scanner.detectionMilliseconds)) ms · CKey: \(String(format: "%.1f", scanner.decodeMilliseconds)) ms · candidates: \(scanner.candidateCount)")
                        .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                    if !scanner.candidateSizes.isEmpty {
                        Text("Border components: \(scanner.candidateSizes)")
                            .font(.caption.monospacedDigit()).foregroundStyle(.tertiary)
                    }
                    Text(scanner.scanDetails)
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(10)
                        .background(.white.opacity(0.06), in: RoundedRectangle(cornerRadius: 10))
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            Button {
                scanner.scanAgain()
            } label: {
                Label(scanner.decodedHex == nil ? "Scanning" : "Scan Again", systemImage: "arrow.clockwise")
                    .frame(maxWidth: .infinity).padding(.vertical, 12)
            }
            .buttonStyle(.borderedProminent)
            .disabled(scanner.decodedHex == nil)

            Spacer(minLength: 0)
        }
        .padding()
        .task { scanner.start() }
        .onDisappear { scanner.stop() }
    }
}

/// What a decoded text payload offers to do, per spec/CKey-Payload-Conventions.md.
/// Only allowlisted forms get an action, and nothing happens until the user
/// taps it; everything else (including javascript:, data:, file:, and unknown
/// app schemes) is shown as plain text only.
enum PayloadAction {
    case web(URL)
    case email(URL, address: String)
    case call(URL, number: String)
    case message(URL, number: String)
    case map(URL, label: String)
    case contact(file: URL, name: String)

    static func interpret(_ text: String) -> PayloadAction? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.uppercased().hasPrefix("BEGIN:VCARD") { return contactCard(trimmed) }
        guard let colon = trimmed.firstIndex(of: ":") else { return nil }
        let scheme = trimmed[..<colon].lowercased()
        let rest = String(trimmed[trimmed.index(after: colon)...])
        let beforeQuery = String(rest.split(separator: "?", maxSplits: 1, omittingEmptySubsequences: false).first ?? "")
        switch scheme {
        case "http", "https":
            guard let url = URL(string: trimmed), url.host != nil else { return nil }
            return .web(url)
        case "mailto":
            let address = beforeQuery.removingPercentEncoding ?? beforeQuery
            guard address.contains("@"), let url = URL(string: trimmed) else { return nil }
            return .email(url, address: address)
        case "tel", "sms":
            // E.164 is recommended (tel:+15551234567); keep only dialable characters.
            let number = beforeQuery.filter { $0.isASCII && ($0.isNumber || $0 == "+") }
            guard number.filter(\.isNumber).count >= 3, let dial = URL(string: "\(scheme):\(number)") else { return nil }
            if scheme == "tel" { return .call(dial, number: number) }
            return .message(URL(string: trimmed) ?? dial, number: number)
        case "geo":
            // RFC 5870: geo:lat,lon[,alt][;params][?q=label]
            let coordinates = beforeQuery.split(separator: ";").first?.split(separator: ",") ?? []
            guard coordinates.count >= 2, let lat = Double(coordinates[0]), let lon = Double(coordinates[1]),
                  (-90.0...90.0).contains(lat), (-180.0...180.0).contains(lon) else { return nil }
            var items = [URLQueryItem(name: "ll", value: "\(lat),\(lon)")]
            let label = URLComponents(string: "geo:x?" + (rest.split(separator: "?", maxSplits: 1).dropFirst().first.map(String.init) ?? ""))?
                .queryItems?.first(where: { $0.name == "q" })?.value
            if let label, !label.isEmpty { items.append(URLQueryItem(name: "q", value: label)) }
            return maps(items, label: label ?? String(format: "%.5f, %.5f", lat, lon))
        case "address":
            // CKey convention: the rest of the payload is a free-text postal
            // address (UTF-8, no percent-encoding); line breaks separate lines.
            let address = rest.split(whereSeparator: \.isNewline)
                .map { $0.trimmingCharacters(in: .whitespaces) }
                .filter { !$0.isEmpty }
                .joined(separator: ", ")
            guard !address.isEmpty else { return nil }
            return maps([URLQueryItem(name: "address", value: address)], label: address)
        default:
            return nil
        }
    }

    /// iOS exposes no public "default map app" API; maps.apple.com links are
    /// routed by the system to Maps.
    private static func maps(_ items: [URLQueryItem], label: String) -> PayloadAction? {
        var components = URLComponents()
        components.scheme = "https"; components.host = "maps.apple.com"; components.path = "/"
        components.queryItems = items
        guard let url = components.url else { return nil }
        return .map(url, label: label)
    }

    /// vCard payloads are offered through the share sheet as a .vcf file,
    /// which lets the user review and add the card in Contacts.
    private static func contactCard(_ card: String) -> PayloadAction? {
        let name = card.split(whereSeparator: \.isNewline)
            .first { $0.uppercased().hasPrefix("FN:") || $0.uppercased().hasPrefix("FN;") }
            .flatMap { line in line.split(separator: ":", maxSplits: 1).dropFirst().first.map(String.init) } ?? "contact"
        let file = FileManager.default.temporaryDirectory.appendingPathComponent("ckey-contact.vcf")
        do { try card.write(to: file, atomically: true, encoding: .utf8) } catch { return nil }
        return .contact(file: file, name: name)
    }
}

struct PayloadActionButton: View {
    let action: PayloadAction
    var body: some View {
        switch action {
        case .web(let url):
            Link(destination: url) { Label("Open \(url.host ?? "link")", systemImage: "safari") }
        case .email(let url, let address):
            Link(destination: url) { Label("Email \(address)", systemImage: "envelope") }
        case .call(let url, let number):
            Link(destination: url) { Label("Call \(number)", systemImage: "phone") }
        case .message(let url, let number):
            Link(destination: url) { Label("Message \(number)", systemImage: "message") }
        case .map(let url, let label):
            Link(destination: url) { Label("Show \(label) in Maps", systemImage: "map") }
        case .contact(let file, let name):
            ShareLink(item: file) { Label("Add \(name) to Contacts…", systemImage: "person.crop.circle.badge.plus") }
        }
    }
}
