// Tests against the built WebAssembly core (dist/). Skipped, with a message,
// when dist/ has not been built (npm run build needs Emscripten).
import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync } from 'node:fs';

const distUrl = new URL('../dist/', import.meta.url);
const built = ['ckey.mjs', 'ckey-core.mjs', 'ckey.wasm'].every((f) => existsSync(new URL(f, distUrl)));
const skip = built ? false : 'dist/ is not built (run `npm run build` with Emscripten); skipping WebAssembly tests';
if (skip && process.env.CKEY_REQUIRE_WASM) throw new Error(`CKEY_REQUIRE_WASM is set but ${skip}`);
if (skip) console.log(`# ${skip}`);

const mod = built ? await import(new URL('ckey.mjs', distUrl).href) : null;

/** Places an RGBA image in the middle of a larger white frame, like a camera view. */
function pad(img, margin) {
  const side = img.width + 2 * margin;
  const data = new Uint8ClampedArray(side * side * 4).fill(255);
  for (let y = 0; y < img.height; y++) {
    data.set(img.data.subarray(y * img.width * 4, (y + 1) * img.width * 4), ((y + margin) * side + margin) * 4);
  }
  return { width: side, height: side, data };
}

async function fresh(extra = {}) {
  const sent = [];
  const ckey = await mod.loadCKey({
    fetch: async (url, init) => (sent.push({ url, init }), { status: 204 }),
    ...extra,
  });
  return { ckey, sent };
}

test('loads, reports its version and the default telemetry mode', { skip }, async () => {
  const { ckey } = await fresh();
  assert.match(ckey.version, /^0\.\d+\.\d+(-(build|dev)\.\d+)?$/);
  assert.equal(ckey.telemetry, 1);
  const again = await mod.loadCKey();
  assert.equal(again, await mod.loadCKey(), 'no-option loads share one instance');
});

test('encode -> renderPixels -> decodeImageData round trip', { skip }, async () => {
  const { ckey } = await fresh();
  for (const [text, opts] of [
    ['https://openckey.org', { version: 0, level: 'M', layout: 'standard' }],
    ['Grüße aus CKey ✓', { level: 'H', layout: 'print' }],
  ]) {
    const sym = ckey.encode(text, opts);
    assert.equal(sym.level, opts.level);
    assert.equal(sym.layout, opts.layout);
    assert.equal(sym.payloadLength, new TextEncoder().encode(text).length);
    assert.ok(sym.capacity >= sym.payloadLength);
    assert.ok(sym.version >= 1 && sym.version <= 31);
    const img = ckey.renderPixels(sym, { size: 512, transparent: false, policy: 'coverage' });
    assert.equal(img.width, 512);
    assert.equal(img.data.length, 512 * 512 * 4);
    assert.ok(img.data instanceof Uint8ClampedArray);
    const r = ckey.decodeImageData(pad(img, 64));
    assert.ok(r, 'decoded');
    assert.equal(r.text, text);
    assert.equal(r.profile, opts.layout === 'print' ? '0.1' : '0');
    assert.equal(r.version, sym.version);
    assert.equal(r.layout, opts.layout);
    sym.free();
    sym.free();
    assert.throws(() => ckey.renderPixels(sym), /freed/);
  }
  const bin = ckey.encode(Uint8Array.of(0, 1, 2, 255));
  const r = ckey.decodeImageData(pad(ckey.renderPixels(bin, { size: 400, preset: 'concept' }), 40));
  assert.deepEqual([...r.bytes], [0, 1, 2, 255]);
  assert.equal(r.text, null);
  assert.equal(ckey.decodeImageData({ width: 64, height: 64, data: new Uint8ClampedArray(64 * 64 * 4).fill(255) }), null);
});

test('transparent output: corner alpha 0, still decodes over white', { skip }, async () => {
  const { ckey } = await fresh();
  const sym = ckey.encode('transparent');
  const img = ckey.renderPixels(sym, { size: 256, transparent: true });
  assert.equal(img.data[3], 0);
  assert.equal(img.data[img.data.length - 1], 0);
  assert.ok(ckey.decodeImageData(pad(img, 32)));
  const opaque = ckey.renderPixels(sym, { size: 256 });
  assert.equal(opaque.data[3], 255);
});

test('PNG, SVG and PDF exports', { skip }, async () => {
  const { ckey } = await fresh();
  const sym = ckey.encode('exports');
  const png = ckey.renderPNG(sym, { size: 128, transparent: true, policy: 'binary' });
  assert.ok(png instanceof Uint8Array);
  assert.deepEqual([...png.subarray(0, 8)], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
  const svg = ckey.renderSVG(sym);
  assert.equal(typeof svg, 'string');
  assert.match(svg, /^<svg[\s\S]*<\/svg>\s*$/);
  assert.match(ckey.renderSVG(sym, { preset: 'concept' }), /<defs>/);
  const pdf = ckey.renderPDF(sym, { widthPt: 144, transparent: false });
  assert.equal(new TextDecoder().decode(pdf.subarray(0, 5)), '%PDF-');
});

test('errors carry the core diagnostics', { skip }, async () => {
  const { ckey } = await fresh();
  assert.throws(() => ckey.encode('x'.repeat(10000)), (e) => e instanceof Error && /exceeds|capacity|payload/i.test(e.message));
  assert.throws(() => ckey.encode('x', { level: 'Z' }), RangeError);
  const sym = ckey.encode('style');
  assert.throws(() => ckey.renderSVG(sym, { style: { ink: '#fafafa' } }), /contrast|:1/);
  assert.equal(ckey.checkStyle(sym, { style: { ink: '#fafafa' } }).ok, false);
  assert.equal(ckey.checkStyle(sym).ok, true);
  if (ckey.abiVersion >= 4) {
    const over = ckey.checkStyle(sym, { style: { ink: '#fafafa', policy: 'override' } });
    assert.equal(over.ok, true);
    assert.match(over.message, /override/);
    assert.ok(ckey.renderSVG(sym, { style: { ink: '#fafafa', policy: 'override' } }).startsWith('<svg'));
    const cert = ckey.certify(sym);
    assert.equal(cert.certified, true);
    const unknown = ckey.certify(sym, { transparent: true });
    assert.equal(unknown.certified, false);
  }
});

test('telemetry mode validation', { skip }, async () => {
  const { ckey } = await fresh();
  for (const bad of [3, -1, '1', 1.5, null, undefined]) {
    assert.throws(() => (ckey.telemetry = bad), RangeError);
    assert.equal(ckey.telemetry, 1, 'never silently changed');
  }
  ckey.telemetry = 2;
  assert.equal(ckey.telemetry, 2);
  ckey.telemetry = 0;
  assert.equal(ckey.telemetry, 0);
  const off = await mod.loadCKey({ telemetry: 0, fetch: null });
  assert.equal(off.telemetry, 0);
  await assert.rejects(mod.loadCKey({ telemetry: 7 }), RangeError);
});

test('scanner drives a session and a mocked send', { skip }, async () => {
  let clock = 10_000;
  const { ckey, sent } = await fresh({ now: () => clock, day: () => '2026-11-05' });
  const sym = ckey.encode('https://openckey.org/scan');
  const frame = pad(ckey.renderPixels(sym, { size: 320 }), 40);
  const blank = { width: 64, height: 64, data: new Uint8ClampedArray(64 * 64 * 4).fill(255) };
  const results = [];
  const scanner = ckey.createScanner({ onResult: (r) => results.push(r) });
  assert.equal(scanner.processFrame(blank), null);
  clock += 100;
  const r = scanner.processFrame(frame);
  assert.equal(r.text, 'https://openckey.org/scan');
  assert.equal(results.length, 1);
  assert.equal(scanner.processFrame(frame), null, 'one result per session');
  scanner.stop();
  const c = ckey.telemetryCounters();
  assert.deepEqual(c.sessions, { started: 1, succeeded: 1, failed: 0 });
  assert.equal(c.frames, 2);
  assert.equal(c.decoded.p0, 1);

  assert.equal(await ckey.flushTelemetry(), null, 'delta younger than a minute');
  clock += 61_000;
  assert.equal(await ckey.flushTelemetry(), 204);
  assert.equal(sent.length, 1);
  assert.equal(sent[0].url, mod.CKEY_TELEMETRY_ENDPOINT);
  assert.equal(sent[0].init.method, 'POST');
  assert.equal(sent[0].init.keepalive, true);
  assert.equal(sent[0].init.headers['content-type'], 'application/json');
  const body = JSON.parse(sent[0].init.body);
  assert.equal(body.sdk, 'web');
  assert.equal(body.platform, 'web');
  assert.equal(body.sdk_version, ckey.version);
  assert.equal(body.day, '2026-11-05');
  assert.deepEqual(body.sessions, { started: 1, succeeded: 1, failed: 0 });
  assert.ok(!sent[0].init.body.includes('openckey.org/scan'), 'no payload content');
  assert.equal(await ckey.flushTelemetry(), null, 'delta empty, hourly cadence');

  // Mode 2 counts locally and never sends.
  ckey.telemetry = 2;
  const s2 = ckey.createScanner();
  s2.processFrame(frame);
  s2.stop();
  clock += 4_000_000;
  assert.equal(await ckey.flushTelemetry(), null);
  assert.equal(sent.length, 1);
  assert.ok(ckey.telemetryCounters().sessions.started >= 2);
});

test('renderToCanvas uses ImageData + putImageData (OffscreenCanvas-like)', { skip }, async () => {
  const { ckey } = await fresh();
  const sym = ckey.encode('canvas');
  let put = null;
  const canvas = {
    width: 10,
    height: 10,
    getContext: (kind) => (kind === '2d' ? {
      createImageData: (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }),
      putImageData: (img, x, y) => (put = { img, x, y }),
    } : null),
  };
  assert.equal(ckey.renderToCanvas(sym, canvas, { size: 64, transparent: false, policy: 'coverage' }), canvas);
  assert.equal(canvas.width, 64);
  assert.equal(put.img.width, 64);
  assert.equal(put.x, 0);
  assert.throws(() => ckey.renderToCanvas(sym, {}), TypeError);
});

test('classifyPayload is exposed on the instance and the module', { skip }, async () => {
  const { ckey } = await fresh();
  assert.equal(ckey.classifyPayload('tel:+15551234567').kind, 'tel');
  assert.equal(mod.classifyPayload('javascript:alert(1)').kind, 'text');
});
