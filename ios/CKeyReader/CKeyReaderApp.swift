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
                        if let url = URL(string: text.trimmingCharacters(in: .whitespacesAndNewlines)),
                           let scheme = url.scheme?.lowercased(), scheme == "https" || scheme == "http", url.host != nil {
                            Link(destination: url) {
                                Label("Open \(url.host ?? "link")", systemImage: "safari")
                            }
                            .font(.callout.weight(.semibold))
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
