import Foundation

/// What a decoded text payload offers to do (spec/CKey-Payload-Conventions.md).
///
/// Classification never acts. The host shows the payload and the proposed
/// action, and acts only when the user taps it, stating the target first
/// ("Open example.com", "Call +1 555..."). Only the allowlisted forms get an
/// action; `javascript:`, `data:`, `file:`, `intent:`, custom app schemes and
/// everything else return `nil` (show as plain text).
public enum CKeyPayloadAction: Equatable, Sendable {
    /// `http:` / `https:` with a host. Show `url.host` before opening.
    case web(URL)
    /// `mailto:` containing an `@`.
    case email(URL, address: String)
    /// `tel:` with at least three digits; `number` keeps only `+` and digits.
    case call(URL, number: String)
    /// `sms:` with at least three digits.
    case message(URL, number: String)
    /// `geo:` (RFC 5870) with valid coordinates. `mapURL` is a
    /// maps.apple.com link (`ll=lat,lon`, plus `q=` for the label).
    case geo(latitude: Double, longitude: Double, label: String?, mapURL: URL)
    /// `address:` (CKey convention): non-empty lines joined with ", ".
    /// `mapURL` is a maps.apple.com `?address=` link.
    case address(String, mapURL: URL)
    /// A complete vCard (`BEGIN:VCARD` ... `END:VCARD`). Offer to add the
    /// contact, for example by sharing it as a .vcf file.
    case contact(vCard: String, name: String)
}

/// Payload interpretation helpers. These never perform an action.
public enum CKeyPayload {
    /// The payload as text when it is valid UTF-8 and contains no control
    /// characters other than tab, LF and CR (CR LF is accepted); otherwise
    /// `nil` (show the payload as binary, for example as hex).
    public static func text(from bytes: Data) -> String? {
        guard !bytes.isEmpty, let text = String(data: bytes, encoding: .utf8) else { return nil }
        // Only C0 controls (except tab, LF, CR), DEL and C1 controls make a
        // payload binary, as the web reader does. Format characters (ZWJ in
        // emoji sequences, soft hyphens, bidi marks, BOM) are ordinary text.
        let printable = text.unicodeScalars.allSatisfy { scalar in
            let v = scalar.value
            if scalar == "\n" || scalar == "\r" || scalar == "\t" { return true }
            return !(v < 0x20 || v == 0x7F || (0x80...0x9F).contains(v))
        }
        return printable ? text : nil
    }

    /// Lower-case hexadecimal, for showing binary payloads.
    public static func hex(_ bytes: Data) -> String {
        bytes.map { String(format: "%02x", $0) }.joined()
    }

    /// Classifies a text payload into a proposed action, or `nil` for plain
    /// text. Schemes match case-insensitively; surrounding whitespace is ignored.
    public static func classify(_ text: String) -> CKeyPayloadAction? {
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.uppercased().hasPrefix("BEGIN:VCARD") { return contactCard(trimmed) }  // vCard 3.0/4.0, RFC 6350
        guard let colon = trimmed.firstIndex(of: ":") else { return nil }
        let scheme = trimmed[..<colon].lowercased()
        let rest = String(trimmed[trimmed.index(after: colon)...])
        let beforeQuery = String(rest.split(separator: "?", maxSplits: 1, omittingEmptySubsequences: false).first ?? "")
        switch scheme {
        case "http", "https":
            guard let url = URL(string: trimmed), let host = url.host, !host.isEmpty else { return nil }
            return .web(url)
        case "mailto":
            let address = beforeQuery.removingPercentEncoding ?? beforeQuery
            guard address.contains("@"), let url = URL(string: trimmed) else { return nil }
            return .email(url, address: address)
        case "tel", "sms":
            // The number ends at the first URI parameter or query (";ext=", "?body="),
            // so parameters are never dialled as digits.
            let numberPart = String(rest.split(whereSeparator: { $0 == "?" || $0 == ";" }).first ?? "")
            let number = numberPart.filter { $0.isASCII && ($0.isNumber || $0 == "+") }
            guard number.filter(\.isNumber).count >= 3, let dial = URL(string: "\(scheme):\(number)") else { return nil }
            if scheme == "tel" { return .call(dial, number: number) }
            return .message(URL(string: trimmed) ?? dial, number: number)
        case "geo":
            let coordinates = beforeQuery.split(separator: ";").first?.split(separator: ",") ?? []
            guard coordinates.count >= 2,
                  let lat = Double(coordinates[0]), let lon = Double(coordinates[1]),
                  (-90.0...90.0).contains(lat), (-180.0...180.0).contains(lon) else { return nil }
            let query = rest.split(separator: "?", maxSplits: 1).dropFirst().first.map(String.init) ?? ""
            let label = URLComponents(string: "geo:x?" + query)?.queryItems?.first(where: { $0.name == "q" })?.value
            var items = [URLQueryItem(name: "ll", value: "\(lat),\(lon)")]
            if let label, !label.isEmpty { items.append(URLQueryItem(name: "q", value: label)) }
            guard let url = mapsURL(items) else { return nil }
            return .geo(latitude: lat, longitude: lon, label: (label?.isEmpty ?? true) ? nil : label, mapURL: url)
        case "address":
            let address = rest.split(whereSeparator: \.isNewline)
                .map { $0.trimmingCharacters(in: .whitespaces) }
                .filter { !$0.isEmpty }
                .joined(separator: ", ")
            guard !address.isEmpty, let url = mapsURL([URLQueryItem(name: "address", value: address)]) else { return nil }
            return .address(address, mapURL: url)
        default:
            return nil
        }
    }

    private static func mapsURL(_ items: [URLQueryItem]) -> URL? {
        var components = URLComponents()
        components.scheme = "https"
        components.host = "maps.apple.com"
        components.path = "/"
        components.queryItems = items
        return components.url
    }

    private static func contactCard(_ card: String) -> CKeyPayloadAction? {
        // A complete card: BEGIN:VCARD on its own line and END:VCARD as the last line.
        let lines = card.split(whereSeparator: \.isNewline).map { $0.trimmingCharacters(in: .whitespaces).uppercased() }
        guard lines.count >= 2, lines.first == "BEGIN:VCARD", lines.last == "END:VCARD" else { return nil }
        let name = card.split(whereSeparator: \.isNewline)
            .first { $0.uppercased().hasPrefix("FN:") || $0.uppercased().hasPrefix("FN;") }
            .flatMap { line in line.split(separator: ":", maxSplits: 1).dropFirst().first.map(String.init) }?
            .trimmingCharacters(in: .whitespaces) ?? "contact"
        return .contact(vCard: card, name: name.isEmpty ? "contact" : name)
    }
}
