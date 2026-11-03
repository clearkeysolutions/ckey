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
                    Text(hex).font(.system(.callout, design: .monospaced)).textSelection(.enabled)
                        .padding(12).frame(maxWidth: .infinity, alignment: .leading)
                        .background(.white.opacity(0.08), in: RoundedRectangle(cornerRadius: 12))
                    Text("Vision: \(String(format: "%.1f", scanner.detectionMilliseconds)) ms · CKey: \(String(format: "%.1f", scanner.decodeMilliseconds)) ms")
                        .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                } else {
                    Text("The phone finds the symbol, then the C++ core checks its keys, error correction, and checksum.")
                        .font(.subheadline).foregroundStyle(.secondary)
                    Text("Vision: \(String(format: "%.1f", scanner.detectionMilliseconds)) ms · CKey: \(String(format: "%.1f", scanner.decodeMilliseconds)) ms · squares: \(scanner.candidateCount)")
                        .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                    if !scanner.candidateSizes.isEmpty {
                        Text("Largest square candidates: \(scanner.candidateSizes) px")
                            .font(.caption.monospacedDigit()).foregroundStyle(.tertiary)
                    }
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
