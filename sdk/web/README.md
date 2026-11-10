# ckey: CKey for the web

Encode, render and scan [CKey](https://openckey.org) optical codes in the browser, in workers and in Node. The package is the CKey C core compiled to WebAssembly, plus a small JavaScript wrapper. Every encode, decode and render call runs in the core. The wrapper only moves bytes in and out.

- Headless rendering to native pixels: `ImageData` on a `<canvas>` or `OffscreenCanvas`, or raw RGBA. There is no SVG step and no DOM work.
- Exports: PNG bytes, an SVG string and PDF vector bytes.
- Scanning of RGBA camera frames. It finds the border, refines it for each profile and decodes, trying Profile 0 first and then Profiles 1, 2 and 3.
- Optional aggregate telemetry. You can turn it off with one line (see [Privacy and telemetry](#privacy-and-telemetry)).

## Install

```sh
npm install openckey
```

The package is ESM only. `dist/ckey.mjs` loads `dist/ckey.wasm` from its own directory, and bundlers that understand `new URL('./ckey.wasm', import.meta.url)` copy the binary for you (Vite, webpack 5, Rollup with a URL plugin, esbuild with a file loader). To serve the binary from somewhere else, pass `wasmUrl`. You can also pass the bytes as `wasmBinary`.

```js
const ckey = await loadCKey({ wasmUrl: '/static/ckey.wasm' });
```

## Usage

### Make a code

```js
import { loadCKey } from 'openckey';

const ckey = await loadCKey();

// text (UTF-8) or bytes; version 0 picks the smallest that fits
const sym = ckey.encode('https://openckey.org', { version: 0, level: 'M', layout: 'standard' });
console.log(sym.version, sym.level, sym.layout, sym.payloadLength, sym.capacity);

// draw into a canvas: ImageData + putImageData, no intermediate file
ckey.renderToCanvas(sym, document.querySelector('canvas'), { size: 512, transparent: false, policy: 'coverage' });

// or use the pixels yourself (also works in Node and in workers)
const { width, height, data } = ckey.renderPixels(sym, { size: 1024 });

// exports
const png = ckey.renderPNG(sym, { size: 1024, transparent: true }); // Uint8Array
const svg = ckey.renderSVG(sym, { preset: 'concept' });             // string
const pdf = ckey.renderPDF(sym, { widthPt: 144 });                  // Uint8Array, 2 inches wide

sym.free(); // release WebAssembly memory now; a forgotten symbol is freed by the GC
```

`level` is `'L'`, `'M'` (default), `'Q'`, `'H'`, `'U'` (Ultra, 80% parity) or `'X'` (Extreme, 95% parity). `U` and `X` spend most of the symbol on parity, and the smallest versions carry nothing at those levels: `ckey.capacity(1, 'X')` is 0 and encoding there throws. `layout` is `'standard'` (default), `'print'` (Profile 0.1), or their byte-grouped forms `'standard-grouped'` (Format 0.29) and `'print-grouped'` (Format 0.30), which keep the same geometry and capacity. Readers before 0.1.0-build.86 cannot read `U`, `X` or the grouped layouts, so they are opt-in. Decoding reports the layout found; `'print-grouped'` reports profile `'0.1'`.

`encode` throws an `Error` that carries the core's diagnostic message, for example `payload exceeds the selected Profile 0 version and level`. An invalid option throws a `TypeError` or a `RangeError`.

#### Render options

| Option | Values | Default |
|---|---|---|
| `size` | 16 to 16384 px across the full 31-unit canvas, quiet margin included | 512 |
| `policy` | `'coverage'` (antialiased), `'binary'` or `'hybrid'` (thin marks widened to `minFeaturePx`) | `'coverage'` |
| `transparent` | `true` paints no background, so unpainted pixels have alpha 0 | `false` |
| `preset` | `'plain'` (black on white) or `'concept'` | `'plain'` |
| `style` | `{ background, backgroundEdge, ink, inkShade, accents, accentPercent, modules, rings, rays, bevel, shadow, texture, logoHref, logoRadius, logoPlate, policy }`, applied on top of the preset | none |
| `supersample`, `minFeaturePx`, `materials`, `logo` | advanced raster settings (see `types/ckey.d.ts`) | 4, 1, `true`, none |

Colours are written as `'#rrggbb'`, `[r, g, b]` or `{ r, g, b }`. Styling changes how a symbol is painted, never where a reader samples. The core checks every style. `style.policy` sets how a style that fails the check is handled:

- `'strict'` (the default) refuses it, and the error message explains why.
- `'adaptive'` repairs it.
- `'override'` draws it as given and leaves the symbol uncertified.

`ckey.checkStyle(sym, { style })` returns the core's verdict. `ckey.certify(sym, { transparent, backdrop })` runs the core's modelled-scan certification. A transparent symbol is never certified unless you describe what it sits on (`backdrop.colours` or `backdrop.pixels`).

### Scan

```js
const scanner = ckey.createScanner({
  onResult(result) {
    // result: { bytes, text, profile: '0' | '0.1' | '1' | '2' | '3', version, layout }
    const action = ckey.classifyPayload(result.text ?? result.bytes);
    showResult(result, action); // never act automatically: let the user tap
  },
});

// for each camera frame (e.g. from requestVideoFrameCallback + a canvas)
ctx.drawImage(video, 0, 0);
scanner.processFrame(ctx.getImageData(0, 0, canvas.width, canvas.height));

scanner.reset(); // scan again after a result
scanner.stop();  // camera closed
```

For a single image, call `ckey.decodeImageData(imageData)`. It returns `null` when no CKey is found, and it also accepts `{ width, height, data }`. RGBA pixels are composited over white and converted to Rec. 709 luma before decoding. If you already have 8-bit grayscale, `ckey.decodeGray({ width, height, data })` skips that conversion.

`text` holds the payload as text when it is valid UTF-8 with no control characters other than tab, LF and CR. Otherwise it is `null`, and you can show `bytes` as hex instead (`toHex` is exported).

### Interpret a payload

`classifyPayload(text)` follows the [CKey Payload Conventions](../../spec/CKey-Payload-Conventions.md). It returns `{ kind, target, label }`, where `kind` is one of `https`, `http`, `mailto`, `tel`, `sms`, `geo`, `address`, `vcard` or `text`. It only describes an action, such as `"Open openckey.org"` or `"Call +15551234567"`, and never performs it. Schemes that are not on the allowlist (`javascript:`, `data:`, `file:`, `intent:`, custom schemes) come back as plain text, as do forms that fail validation. The function is also exported from the module, so you can call it without loading the core.

## Safe Actions

A Safe Action (`ckey-action:1:…`) is a signed code from a known site for this app. `ckey.safeActions()` verifies it with the core and decides whether it runs; the page only pins keys and registers handlers.

```js
const actions = ckey.safeActions('com.viadere')
  .trust('citeory.com', '2026-11', '<64 hex digits>', { requestInfo: 'https://citeory.com/api/ckey/requests/{request_id}' })
  .register('open.library', { issuer: 'citeory.com' }, (action) => openItem(action.paramsJSON))
  .register('signin.request', { issuer: 'citeory.com', scope: CROSS_DEVICE }, (action) => approveSignIn(action));

const outcome = await actions.handle(text);
if (outcome.kind === 'not-safe-action') showPayload(text);        // the ordinary payload rules
else if (outcome.kind === 'confirm') await outcome.request.present(); // the SDK's own <dialog>
else if (outcome.kind === 'rejected') showUnverified(outcome.reason);
```

Same-device actions run at once. Cross-device actions run only from `request.present()`, which shows the verified issuer and who is asking; there is no other way to approve one. Anything that fails a check runs nothing; show that the code claims to be a Safe Action but could not be verified, with the reason. See [spec/CKey-Safe-Actions.md](../../spec/CKey-Safe-Actions.md).

## Privacy and telemetry

**Opt out with one line:**

```js
ckey.telemetry = 0;
```

You can also opt out at load time with `loadCKey({ telemetry: 0 })`, which applies before anything is counted.

| `ckey.telemetry` | Counts locally | Sends |
|---|---|---|
| `0` | no | no |
| `1` (default) | yes | aggregate batches, at most one per hour |
| `2` | yes | never (`ckey.telemetryCounters()` returns the counts) |

Any other value throws a `RangeError`. The mode is never silently reset to 1.

Only the scanner helper (`createScanner`) records anything. Encoding, rendering and one-off `decodeImageData` calls never do. A scan session starts at the first frame, succeeds at the first decode and ends at `stop()`. A session with no decode for 30 seconds fails.

A batch holds only these fields:

- session counts;
- the number of frames;
- decodes per profile;
- two duration histograms;
- `sdk: "web"`, the SDK version and `platform: "web"`;
- the UTC day.

A batch never contains payloads, URLs, identifiers, fingerprints or location. Batches go by `fetch` POST to `https://openckey.org/api/telemetry/v1/batch` with `keepalive`, no cookies and no `Referer`. A send runs asynchronously, never blocks scanning, and is also tried when the page is hidden.

The full rules are in [docs/adoption-and-telemetry.md](../../docs/adoption-and-telemetry.md). Your own privacy disclosures may still need to mention telemetry while mode 1 is on.

For tests or a custom transport, pass `loadCKey({ fetch })`. `fetch: null` never sends.

## Build from source

You need [Emscripten](https://emscripten.org) (`emcmake` and `em++` on `PATH`), CMake 3.20 or later and Node 22.

```sh
npm run build        # emcmake cmake + build, then assemble dist/
npm test             # node:test; the wasm tests skip when dist/ is missing
npm run test:native  # bridge + core compiled with the system C++ compiler
npm run check        # node --check every JS file
```

| Path | Contents |
|---|---|
| `src/ckey_web.cpp` | the bridge: plain exported C functions over the public C ABI |
| `js/` | the wrapper (`ckey.mjs` loader, `api.mjs`, and pure modules for options, payloads, telemetry and the scanner) |
| `types/ckey.d.ts` | TypeScript declarations, copied to `dist/` |

## License

MIT. See [LICENSE](LICENSE).
