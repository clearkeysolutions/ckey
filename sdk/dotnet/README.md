# CKey for .NET

CKey optical codes for .NET. The package encodes payloads, renders native pixels (and exports PNG, SVG and PDF), and scans grayscale camera frames. It is a thin P/Invoke wrapper over the CKey C core and does not reimplement encoding, decoding or rendering.

| Package | Target | Contents |
|---|---|---|
| `CKey` | `net8.0`, `netstandard2.0` | Headless core: encode, render, certify, scan, payload helpers, telemetry. No UI dependency. |
| `CKey.Wpf` | `net8.0-windows` | Optional adapter: `WriteableBitmap` output and `BitmapSource` scanning. |

Native libraries ship inside the package as `runtimes/<rid>/native/` assets for `win-x64`, `linux-x64`, `osx-x64` and `osx-arm64`. On `net8.0` the bindings use `[LibraryImport]` and are trim- and AOT-compatible. On `netstandard2.0` they use `[DllImport]`. .NET Framework apps get `ckeynative.dll` (win-x64) copied by the package's build targets.

## Install

```sh
dotnet add package CKey
dotnet add package CKey.Wpf   # optional, WPF only
```

```xml
<PackageReference Include="CKey" Version="0.1.0-build.83" />
```

The namespace is `OpenCKey`.

## Encode and render

```csharp
using OpenCKey;

CKeySymbol symbol = CKeySymbol.Encode("https://openckey.org/hello");      // version 0 = smallest that fits
CKeySymbol big = CKeySymbol.Encode(bytes, version: 12, level: EccLevel.Q, layout: Layout.Print);

// Native pixels: size x size RGBA8, straight alpha, no padding.
CKeyImage image = symbol.RenderRgba(512, RasterPolicy.Coverage, transparent: false, style: CKeyStyle.Concept);
byte[] rgba = image.Rgba;

// Exports
byte[] png = symbol.ToPng(1024);
string svg = symbol.ToSvg(scale: 8, style: CKeyStyle.Plain);
byte[] pdf = symbol.ToPdf(widthPt: 144);                 // 2 inches wide, vector
CKeyGrayImage print = symbol.RenderPrint(1.0, 600);      // 1 inch at 600 dpi, pure black/white
```

`CKeySymbol` is a managed copy of the C symbol struct. It holds no native resources, so it needs no `Dispose`.

### Styles and optical safety

```csharp
var style = CKeyStyle.Concept;                // or CKeyStyle.Plain, or new CKeyStyle()
style.Modules = ModuleShape.Circle;
style.Ink = CKeyColor.Parse("#1a1a2e");

if (!symbol.CheckStyle(style, out string why)) Console.WriteLine(why);

// Certification renders the style, degrades it with a camera model and decodes it.
CKeyCertification c = symbol.Certify(style);
// A transparent symbol needs to know what it sits on:
c = symbol.Certify(null, transparent: true, backdropColours: new[] { CKeyColor.White });
```

Under the default `StylePolicy.Strict`, renderers refuse a style that fails the checks and throw `CKeyException`. With `StylePolicy.Adaptive`, the style is repaired instead (see `CKeyCertification.AdjustedStyle`). `StylePolicy.Override` draws the style as given.

### WPF

```csharp
using OpenCKey.Wpf;

imageControl.Source = symbol.ToWriteableBitmap(512, transparent: true);
```

## Scan

```csharp
// gray: one 8-bit luma plane (for example the Y plane of NV12), any stride.
CKeyResult? result = CKeyDecoder.Decode(gray, width, height, stride);
if (result != null)
{
    Console.WriteLine(result.ProfileName);    // "Profile 0 · v3"
    Console.WriteLine(result.Text ?? result.PayloadHex);
    CKeyPayloadAction? action = result.Action;  // e.g. Kind = Web, Label = "Open openckey.org"
    // Show the payload and the action; act only when the user confirms.
}
```

The pipeline is the iOS reader's. It finds up to six border candidates (`CKeyDecoder.FindBorders`). For each one it refines the quadrilateral to the Profile 0 field and decodes, then tries the Profile 1, 2 and 3 fields. Detection and decode are also available separately, via `FindBorders` and `DecodeCandidate`. Scanning never needs the renderer, telemetry or a network.

For continuous camera scanning, use a session. It also drives the telemetry session counters:

```csharp
using var session = new CKeyScanSession();       // starts a scan session
foreach (Frame f in camera.Frames)
{
    if (session.ProcessFrame(f.Gray, f.Width, f.Height, f.Stride) is { } r) { Show(r); break; }
}
// Dispose ends the session (failed if nothing decoded). Restart() is "scan again".
```

`CKeyPayload.Classify(text)` maps text to an action using spec/CKey-Payload-Conventions.md. Only `https`/`http`, `mailto`, `tel`, `sms`, `geo`, `address:` and vCards are recognized; anything else is plain text. `CKeyPayload.ReadableText(bytes)` applies the convention's text rule.

## Telemetry

The default mode is `1`. In mode 1 the SDK counts scan sessions, frames, decodes per profile and coarse durations. Roughly once an hour it sends one anonymous aggregate batch to `https://openckey.org/api/telemetry/v1/batch`. It never sends payloads, URLs, user or device identifiers, or location. Sending happens on a thread-pool timer and never blocks or delays scanning. Only `CKeyScanSession` records anything; `CKeyDecoder` and rendering do not. See [docs/adoption-and-telemetry.md](../../docs/adoption-and-telemetry.md).

To opt out:

```csharp
CKey.Telemetry = 0;   // off: nothing counted, nothing sent, counters purged
CKey.Telemetry = 2;   // local only: counters via CKeyTelemetry.Snapshot(), never sent
CKey.Telemetry = 1;   // default
```

Any other value throws `ArgumentOutOfRangeException`, and the mode stays unchanged.

`CKeyTelemetry.FlushAsync()` tries a send immediately, for example when your app is deactivated or moves to the background (the hourly limit still applies). `ExportState()` and `ImportState()` persist counters across restarts if you want that; persistence is off unless you store the state. A native library built with `-DCKEY_DOTNET_TELEMETRY=OFF` contains no telemetry code at all. In that case `CKeyTelemetry.IsAvailable` is false and the mode reads 0.

Disclose telemetry according to your own app-store and legal requirements.

## Building from source

```sh
# Native library for this machine -> sdk/dotnet/artifacts/native/<rid>/
cmake -S sdk/dotnet/native -B build-dotnet -DCMAKE_BUILD_TYPE=Release -DCKEY_DOTNET_RID=linux-x64
cmake --build build-dotnet --config Release

# Check the C struct layouts the C# interop mirrors
./build-dotnet/ckey_layout_probe | diff sdk/dotnet/native/tools/layout_expected.txt -

dotnet test sdk/dotnet/tests/CKey.Tests -p:CKeyHostRid=linux-x64
dotnet pack sdk/dotnet/src/CKey -c Release     # add -p:CKeyRequireAllNatives=true to insist on all four RIDs
```

The package version comes from the repository's `VERSION` file.

## License

MIT.
