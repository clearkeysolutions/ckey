// Pure-JS tests of the wrapper's marshalling (js/api.mjs) against a fake
// Emscripten module that mimics the bridge's C contract: malloc'd results +
// ckw_last_len, NUL-terminated strings, u32/f64 out-parameters, and a heap
// that GROWS (replacing HEAPU8) so stale-view bugs show up. It also checks
// that every temporary allocation is freed. The real core is exercised by
// wasm.test.mjs (CI) and test/native/bridge_test.cpp.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createCKey } from '../js/api.mjs';
import { loadCKey } from '../js/ckey.mjs';

function fakeModule() {
  const enc = new TextEncoder();
  const M = { HEAPU8: new Uint8Array(1024) };
  let top = 8;
  const live = new Map(); // ptr -> size
  const symbols = new Map(); // ptr -> { payload, version, level, layout }
  const styles = new Map();
  let lastLen = 0;
  let errPtr = 0;
  const tel = { mode: 1, client: null, events: [], batchId: 0, inflight: 0 };

  M._ckw_malloc = (n) => {
    n = Math.max(1, n);
    if (top + n > M.HEAPU8.length) {
      const grown = new Uint8Array(Math.max(M.HEAPU8.length * 2, top + n + 1024));
      grown.set(M.HEAPU8);
      M.HEAPU8 = grown; // views on the old buffer are now stale
    }
    const p = top;
    top += (n + 7) & ~7;
    live.set(p, n);
    return p;
  };
  M._ckw_free = (p) => {
    if (p) {
      assert.ok(live.has(p), `free of unknown pointer ${p}`);
      live.delete(p);
    }
  };
  const putBytes = (bytes) => {
    const p = M._ckw_malloc(bytes.length);
    M.HEAPU8.set(bytes, p);
    lastLen = bytes.length;
    return p;
  };
  const putCString = (s, track = true) => {
    const b = enc.encode(s);
    const p = M._ckw_malloc(b.length + 1);
    M.HEAPU8.set(b, p);
    M.HEAPU8[p + b.length] = 0;
    if (track) lastLen = b.length;
    return p;
  };
  const readCString = (p) => {
    let e = p;
    while (M.HEAPU8[e]) e++;
    return new TextDecoder().decode(M.HEAPU8.subarray(p, e));
  };
  const setError = (s) => {
    if (errPtr) M._ckw_free(errPtr);
    errPtr = putCString(s, false);
  };
  setError('');
  const u32 = (p, i, v) => new DataView(M.HEAPU8.buffer).setUint32(p + 4 * i, v, true);
  const f64 = (p, i, v) => new DataView(M.HEAPU8.buffer).setFloat64(p + 8 * i, v, true);

  M._ckw_last_error = () => errPtr;
  M._ckw_last_len = () => lastLen;
  const versionPtr = putCString('0.1.0-build.63', false);
  M._ckw_software_version = () => versionPtr;
  M._ckw_abi_version = () => 4;
  M._ckw_scalable_capacity = () => 100;
  M._ckw_scalable_modules_across = (v) => 28 + v;
  M._ckw_encode = (p, len, version, level, layout) => {
    if (len > 100) return setError('payload exceeds the selected Profile 0 version and level'), 0;
    const s = M._ckw_malloc(8192);
    symbols.set(s, { payload: M.HEAPU8.slice(p, p + len), version: version || 3, level, layout });
    return s;
  };
  M._ckw_symbol_free = (s) => {
    assert.ok(symbols.delete(s), 'double symbol free');
    M._ckw_free(s);
  };
  M._ckw_symbol_info = (s, out) => {
    const x = symbols.get(s);
    [x.version, x.level, x.layout, x.payload.length, 1].forEach((v, i) => u32(out, i, v));
  };
  M._ckw_style_new = (preset) => {
    if (preset > 1) return setError('unknown style preset'), 0;
    const p = M._ckw_malloc(64);
    styles.set(p, { preset, rgb: {}, num: {}, logo: null });
    return p;
  };
  M._ckw_style_free = (p) => (styles.delete(p), M._ckw_free(p));
  M._ckw_style_set_rgb = (p, f, r, g, b) => ((styles.get(p).rgb[f] = [r, g, b]), 1);
  M._ckw_style_set_num = (p, f, v) => ((styles.get(p).num[f] = v), 1);
  M._ckw_style_set_logo = (p, s) => ((styles.get(p).logo = readCString(s)), 1);
  const refused = (ws) => ws && styles.get(ws).rgb[2]?.[0] > 200 && styles.get(ws).num[10] !== 2;
  M._ckw_style_check = (s, ws) => (refused(ws) ? (setError('ink is 1.0:1; need 3:1'), 1) : (setError('style OK'), 0));
  M._ckw_render_raster = (s, ws, size, policy, transparent) => {
    if (refused(ws)) return setError('ink is 1.0:1; need 3:1'), 0;
    const px = new Uint8Array(size * size * 4).fill(255);
    if (transparent) for (let i = 3; i < px.length; i += 4) px[i] = 0;
    px[1] = [...symbols.keys()].indexOf(s); // the fake "symbol" the decoder recognises
    px[2] = 0xc4;
    return putBytes(px);
  };
  M._ckw_encode_png_rgba = (p, w, h) => putBytes(Uint8Array.of(0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, w, h));
  M._ckw_render_svg = (s, ws, scale) => (refused(ws) ? (setError('ink is 1.0:1; need 3:1'), 0) : putCString(`<svg width="${31 * scale}"></svg>`));
  M._ckw_render_pdf = () => putBytes(enc.encode('%PDF-1.4\n'));
  // A 3x3 lattice: ink on the diagonal, accent 0 in the centre.
  M._ckw_render_lattice = (s, ws, cells) => {
    if (cells > 6) return setError('invalid lattice options (cells per module 0..6)'), 0;
    const out = new Uint8Array(22 + 9);
    out[0] = 3;
    [255, 255, 255, 0, 0, 0, 200, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0].forEach((v, i) => (out[4 + i] = v));
    [1, 0, 0, 0, 2, 0, 0, 0, 1].forEach((v, i) => (out[22 + i] = v));
    return putBytes(out);
  };
  M._ckw_has_certify = () => 1;
  M._ckw_certify = (s, ws, transparent, colours, n, px, w, h, ppm, blur, noise, glare, trials, out) => {
    [transparent ? 0 : 1, 0, 1 | 32, 3, transparent ? 0 : 3, 21, 0.9, 0].forEach((v, i) => f64(out, i, v));
    setError(transparent && !n ? 'background unknown' : 'certified');
    return transparent && !n ? 1 : 0;
  };
  const decode = (p, w, h, stride, info, channels) => {
    if (channels === 4 && M.HEAPU8[p + 2] === 0xc4) {
      const s = [...symbols.keys()][M.HEAPU8[p + 1]];
      const x = symbols.get(s);
      [x.layout & 1, x.version, x.layout, 1].forEach((v, i) => u32(info, i, v));
      return putBytes(x.payload);
    }
    setError('no CKey border found');
    return 0;
  };
  M._ckw_decode_rgba = (p, w, h, s, info) => decode(p, w, h, s, info, 4);
  M._ckw_decode_gray = (p, w, h, s, info) => decode(p, w, h, s, info, 1);
  M._ckw_tel_set_mode = (m) => (m >= 0 && m <= 2 ? ((tel.mode = m), 0) : 1);
  M._ckw_tel_get_mode = () => tel.mode;
  M._ckw_tel_set_client = (a, b, c) => ((tel.client = [readCString(a), readCString(b), readCString(c)]), 0);
  M._ckw_tel_session_begin = (t) => tel.events.push(['begin', t]);
  M._ckw_tel_frame = (t, ms) => tel.events.push(['frame', t, ms]);
  M._ckw_tel_session_success = (t, code) => (tel.events.push(['success', t, code]), 0);
  M._ckw_tel_session_end = (t) => tel.events.push(['end', t]);
  M._ckw_tel_should_send = () => (tel.mode === 1 && !tel.inflight ? 1 : 0);
  M._ckw_tel_take_batch = (day) => {
    tel.inflight = ++tel.batchId;
    return putCString(`{"schema":1,"sdk":"web","day":"${readCString(day)}"}`);
  };
  M._ckw_tel_last_batch_id = () => tel.batchId;
  M._ckw_tel_batch_result = (id, status) => (id === tel.inflight ? ((tel.inflight = 0), (tel.status = status), 0) : 1);
  M._ckw_tel_snapshot = (p) => {
    for (let i = 0; i < 33; i++) f64(p, i, i);
    return 0;
  };
  M._ckw_tel_reset_for_testing = () => {};
  return { M, live, symbols, tel, base: () => new Set([errPtr, versionPtr]) };
}

const leaks = (f) => [...f.live.keys()].filter((p) => !f.base().has(p) && !f.symbols.has(p));

test('encode, render, decode marshalling with a growing heap and no leaks', () => {
  const f = fakeModule();
  const ckey = createCKey(f.M, { fetch: null });
  assert.equal(ckey.version, '0.1.0-build.63');
  assert.deepEqual(f.tel.client, ['web', '0.1.0-build.63', 'web']);
  const sym = ckey.encode('héllo', { level: 'Q', layout: 'print' });
  assert.deepEqual(
    { v: sym.version, l: sym.level, y: sym.layout, n: sym.payloadLength, c: sym.capacity, m: sym.modules },
    { v: 3, l: 'Q', y: 'print', n: 6, c: 100, m: 31 },
  );
  // Levels U and X and the grouped layouts map to the core's codes 4, 5 and 2, 3.
  const ultra = ckey.encode('u', { level: 'U', layout: 'standard-grouped' });
  assert.deepEqual([f.symbols.get([...f.symbols.keys()].at(-1)).level, f.symbols.get([...f.symbols.keys()].at(-1)).layout], [4, 2]);
  assert.deepEqual([ultra.level, ultra.layout], ['U', 'standard-grouped']);
  ultra.free();
  const extreme = ckey.encode('x', { level: 'X', layout: 'print-grouped' });
  assert.deepEqual([extreme.level, extreme.layout], ['X', 'print-grouped']);
  extreme.free();
  const img = ckey.renderPixels(sym, { size: 64, transparent: true }); // forces heap growth
  assert.equal(img.data.length, 64 * 64 * 4);
  assert.equal(img.data[7], 0);
  const r = ckey.decodeImageData(img);
  assert.equal(r.text, 'héllo');
  assert.equal(r.profile, '0.1');
  assert.equal(r.layout, 'print');
  assert.equal(r.version, 3);
  assert.equal('profileCode' in r, false);
  assert.equal(ckey.decodeImageData({ width: 2, height: 2, data: new Uint8ClampedArray(16) }), null);
  assert.equal(ckey.decodeGray({ width: 2, height: 2, data: new Uint8Array(4) }), null);
  assert.deepEqual([...ckey.renderPNG(sym, { size: 32 }).subarray(0, 4)], [0x89, 0x50, 0x4e, 0x47]);
  assert.equal(ckey.renderSVG(sym, { scale: 2 }), '<svg width="62"></svg>');
  assert.equal(new TextDecoder().decode(ckey.renderPDF(sym)).slice(0, 5), '%PDF-');
  const lat = ckey.renderLattice(sym);
  assert.equal(lat.side, 3);
  assert.deepEqual([...lat.cells], [1, 0, 0, 0, 2, 0, 0, 0, 1]);
  const pp = ckey.renderPixelPerfect(sym, { pxPerCell: 2 });
  assert.equal(pp.width, 6);
  assert.deepEqual([...pp.data.subarray(0, 4)], [0, 0, 0, 255]); // ink cell, whole pixels
  assert.deepEqual([...pp.data.subarray(4, 8)], [0, 0, 0, 255]);
  assert.deepEqual([...pp.data.subarray(8, 12)], [255, 255, 255, 255]);
  assert.equal(ckey.renderPixelPerfect(sym, { transparent: true }).data[7], 0);
  assert.equal(ckey.pixelPerfectScale(sym, 460), 153);
  const svg = ckey.renderSVGPixelPerfect(sym, { pxPerCell: 4 });
  assert.match(svg, /width="12"/);
  assert.match(svg, /shape-rendering="crispEdges"/);
  assert.match(svg, /<path fill="#000000" d="M0 0h1v1h-1zM2 2h1v1h-1z"\/>/);
  assert.match(svg, /<path fill="#c80000" d="M1 1h1v1h-1z"\/>/);
  assert.throws(() => ckey.renderLattice(sym, { cellsPerModule: 7 }), RangeError);
  assert.throws(() => ckey.renderPixelPerfect(sym, { pxPerCell: 1.5 }), RangeError);
  assert.deepEqual([...ckey.renderPNGPixelPerfect(sym).subarray(0, 4)], [0x89, 0x50, 0x4e, 0x47]);
  assert.throws(() => ckey.encode('x'.repeat(101)), /payload exceeds/);
  assert.throws(() => ckey.renderSVG(sym, { style: { ink: '#fafafa' } }), /need 3:1/);
  assert.ok(ckey.renderSVG(sym, { style: { ink: '#fafafa', policy: 'override' } }));
  assert.deepEqual(ckey.checkStyle(sym, { preset: 'concept' }), { ok: true, message: 'style OK' });
  const cert = ckey.certify(sym, { backdrop: { colours: ['#000'] } });
  assert.equal(cert.certified, true);
  assert.deepEqual(cert.adjustments, ['ink', 'materials']);
  assert.equal(cert.policy, 'strict');
  assert.equal(ckey.certify(sym, { transparent: true }).ok, false);
  assert.deepEqual(leaks(f).filter((p) => f.live.get(p) !== 64 * 64 * 4), [], 'only the frame scratch buffer stays');
  sym.free();
  sym.free();
  assert.equal(sym.freed, true);
  assert.throws(() => ckey.renderSVG(sym), /freed/);
  assert.throws(() => ckey.renderSVG({}), TypeError);
});

test('telemetry property, counters and scanner wiring', async () => {
  const f = fakeModule();
  let clock = 0;
  const sent = [];
  const ckey = createCKey(f.M, { fetch: async (u, init) => (sent.push(init.body), { status: 204 }), now: () => (clock += 10), day: () => '2026-01-02' });
  assert.equal(ckey.telemetry, 1);
  assert.throws(() => (ckey.telemetry = 5), RangeError);
  assert.equal(ckey.telemetry, 1);
  ckey.telemetry = 2;
  assert.equal(f.tel.mode, 2);
  ckey.telemetry = 1;
  const sym = ckey.encode('scan me');
  const frame = ckey.renderPixels(sym, { size: 16 });
  const got = [];
  const sc = ckey.createScanner({ onResult: (r) => got.push(r.text) });
  sc.processFrame(frame);
  sc.stop();
  assert.deepEqual(f.tel.events.map((e) => e[0]), ['begin', 'frame', 'success', 'end']);
  assert.deepEqual(got, ['scan me']);
  assert.equal(await ckey.flushTelemetry(), 204);
  assert.deepEqual(sent, ['{"schema":1,"sdk":"web","day":"2026-01-02"}']);
  assert.equal(f.tel.status, 204);
  const c = ckey.telemetryCounters();
  assert.deepEqual(c.sessions, { started: 0, succeeded: 1, failed: 2 });
  assert.deepEqual(c.time_to_decode_ms.buckets, [11, 12, 13, 14, 15, 16, 17, 18, 19, 20]);
  assert.equal(c.frame_ms.count, 21);
  ckey._testing.sender.disarm();
  assert.throws(() => createCKey(fakeModule().M, { telemetry: '0' }), RangeError);
  assert.equal(createCKey(fakeModule().M, { telemetry: 0 }).telemetry, 0);
});

test('loadCKey with an injected factory and wasm binary', async () => {
  const f = fakeModule();
  let args;
  const ckey = await loadCKey({
    moduleFactory: async (a) => ((args = a), f.M),
    wasmBinary: new Uint8Array([0, 97, 115, 109]),
    wasmUrl: 'https://cdn.example/ckey/ckey.wasm',
    fetch: null,
  });
  assert.equal(args.locateFile('ckey-core.wasm'), 'https://cdn.example/ckey/ckey.wasm');
  assert.equal(args.locateFile('other.js'), 'other.js');
  assert.ok(args.wasmBinary instanceof Uint8Array);
  assert.equal(ckey.classifyPayload('https://openckey.org').label, 'Open openckey.org');
  assert.throws(() => loadCKey(5), TypeError);
});
