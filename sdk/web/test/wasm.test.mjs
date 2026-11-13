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
    ['grouped ✓', { level: 'U', layout: 'standard-grouped' }],
    ['grouped print ✓', { level: 'Q', layout: 'print-grouped' }],
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
    assert.equal(r.profile, opts.layout.startsWith('print') ? '0.1' : '0');
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
  assert.equal(ckey.capacity(1, 'X'), 0);
  assert.throws(() => ckey.encode('x', { version: 1, level: 'X' }), Error);
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

test('Safe Actions verify through the wasm core and agree with the conformance vectors', { skip }, async () => {
  const { readFileSync } = await import('node:fs');
  const { vectors } = JSON.parse(readFileSync(new URL('../../../tests/vectors/safe-actions-v1.json', import.meta.url), 'utf8'));
  const { ckey } = await fresh();
  for (const v of vectors.filter((x) => x.fields || x.expect === 'integrity')) {
    const app = v.fields?.app ?? 'com.viadere';
    let ran = 0;
    const sa = ckey.safeActions(app, { now: () => v.now * 1000, maxLifetime: v.max_lifetime })
      .trust(v.fields?.issuer ?? 'citeory.com', v.fields?.key_id ?? '2026-11', v.public_key)
      .register(v.fields?.action ?? 'open.library', { issuer: v.fields?.issuer ?? 'citeory.com' }, () => ++ran);
    const out = await sa.handle(v.payload);
    if (v.expect === 'ok' && v.fields.scope === 0) assert.equal(out.kind, 'ran', v.name);
    if (v.expect !== 'ok') assert.equal(out.kind, 'rejected', v.name);
  }
});

test('pixel-perfect: whole cells, decodes, and canvas/PNG/SVG agree', { skip }, async () => {
  const { ckey } = await fresh();
  for (const layout of ['standard', 'print']) {
    const text = 'pixel-perfect ' + layout + ' '.padEnd(300, 'x');
    const sym = ckey.encode(text, { layout });
    const lat = ckey.renderLattice(sym);
    assert.equal(lat.cells.length, lat.side * lat.side);
    assert.ok(lat.cells.every((v) => v <= 1));
    const img = ckey.renderPixelPerfect(sym, { pxPerCell: 2 });
    assert.equal(img.width, lat.side * 2);
    // Curves are antialiased (grey edge pixels), while every grid module is
    // exact: the lattice's ink cells are pure ink in the aligned raster.
    // (Ring and ray cells snapped at 10% coverage are partly grey there.)
    const cells = ckey.renderLattice(sym, {});
    let ink = 0, dark = 0, grey = 0;
    for (let i = 0; i < cells.cells.length; i++) {
      const y = Math.floor(i / cells.side), x = i % cells.side;
      const v = img.data[((2 * y) * img.width + 2 * x) * 4];
      if (v > 20 && v < 235) grey++;
      if (cells.cells[i] !== 1) continue;
      ink++; if (v < 128) dark++;
    }
    assert.ok(dark / ink > 0.75, `ink cells dark in the aligned raster: ${dark}/${ink}`);
    assert.ok(grey > 0, 'curves are antialiased');
    // Pad like a photo and decode with the core.
    const pad = 40, w = img.width + 2 * pad;
    const frame = new Uint8ClampedArray(w * w * 4).fill(255);
    for (let y = 0; y < img.height; y++) frame.set(img.data.subarray(y * img.width * 4, (y + 1) * img.width * 4), ((y + pad) * w + pad) * 4);
    const r = ckey.decodeImageData({ width: w, height: w, data: frame });
    assert.ok(r, layout + ' pixel-perfect render decodes');
    assert.equal(r.text, text);
    const png = ckey.renderPNGPixelPerfect(sym, { pxPerCell: 3, dpi: 300 });
    assert.deepEqual([...png.subarray(0, 4)], [0x89, 0x50, 0x4e, 0x47]);
    const svg = ckey.renderSVGPixelPerfect(sym, { pxPerCell: 3 });
    assert.match(svg, new RegExp(`width="${lat.side * 3}"`));
    assert.match(svg, /shape-rendering="crispEdges"/);
    assert.equal(ckey.pixelPerfectScale(sym, lat.side * 4 + 3), 4);
    // The fit spends spare pixels on cells per module, and stays pixel-perfect and decodable.
    const fit = ckey.pixelPerfectFit(sym, 900);
    assert.ok(fit.cellsPerModule > (layout === 'print' ? 3 : 6) && fit.devicePx <= 900, JSON.stringify(fit));
    assert.equal(fit.belowMinimum, false);
    // A box too small for the scannable minimum: the default never goes
    // below it; allowBelowMinimum fits the box and says so.
    const tight = Math.floor(fit.side / fit.cellsPerModule * 2);
    assert.equal(ckey.pixelPerfectFit(sym, tight).cellsPerModule, fit.minimumCellsPerModule);
    const small = ckey.pixelPerfectFit(sym, tight, { allowBelowMinimum: true });
    assert.ok(small.belowMinimum && small.cellsPerModule < small.minimumCellsPerModule && small.devicePx <= tight, JSON.stringify(small));
    assert.equal(ckey.pixelPerfectFit(sym, 4, { allowBelowMinimum: true }).overflow, true);
    const fine = ckey.renderPixelPerfect(sym, { cellsPerModule: fit.cellsPerModule });
    assert.equal(fine.width, fit.side);
    const w2 = fine.width + 80, f2 = new Uint8ClampedArray(w2 * w2 * 4).fill(255);
    for (let y = 0; y < fine.height; y++) f2.set(fine.data.subarray(y * fine.width * 4, (y + 1) * fine.width * 4), ((y + 40) * w2 + 40) * 4);
    assert.equal(ckey.decodeImageData({ width: w2, height: w2, data: f2 }).text, text);
    sym.free();
  }
});

test('pixel layout: encodes, reads back as pixel, and its lattice decodes at 2 cells per module', { skip }, async () => {
  const { ckey } = await fresh();
  const text = 'pixel layout '.padEnd(400, 'p');
  const sym = ckey.encode(text, { layout: 'pixel' });
  assert.equal(sym.layout, 'pixel');
  const lat = ckey.renderLattice(sym, { cellsPerModule: 2 });
  const img = ckey.renderPixelPerfect(sym, { cellsPerModule: 2, pxPerCell: 1 });
  assert.equal(img.width, lat.side);
  const pad = 40, w = img.width + 2 * pad;
  const frame = new Uint8ClampedArray(w * w * 4).fill(255);
  for (let y = 0; y < img.height; y++) frame.set(img.data.subarray(y * img.width * 4, (y + 1) * img.width * 4), ((y + pad) * w + pad) * 4);
  const r = ckey.decodeImageData({ width: w, height: w, data: frame });
  assert.ok(r, 'the pixel layout decodes at 2 pixels per module');
  assert.equal(r.text, text);
  assert.equal(r.layout, 'pixel');
  assert.equal(r.profile, '0');
  sym.free();
});
