// Pure-JS tests: option validation.
import test from 'node:test';
import assert from 'node:assert/strict';
import {
  encodeOptions, rasterOptions, svgOptions, pdfOptions, styleSpec, parseColour, telemetryMode, payloadBytes,
  rgbaFrame, certifyOptions,
} from '../js/options.mjs';

test('encode options', () => {
  assert.deepEqual(encodeOptions(), { version: 0, level: 1, layout: 0 });
  assert.deepEqual(encodeOptions({ version: 31, level: 'H', layout: 'print' }), { version: 31, level: 3, layout: 1 });
  assert.equal(encodeOptions({ level: 'q' }).level, 2);
  assert.throws(() => encodeOptions({ version: 32 }), RangeError);
  assert.throws(() => encodeOptions({ version: 1.5 }), TypeError);
  assert.throws(() => encodeOptions({ level: 'X' }), RangeError);
  assert.throws(() => encodeOptions({ layout: 'poster' }), RangeError);
  assert.throws(() => encodeOptions('M'), TypeError);
});

test('raster options', () => {
  const d = rasterOptions();
  assert.equal(d.size, 512);
  assert.equal(d.policy, 0);
  assert.equal(d.transparent, false);
  assert.equal(d.supersample, 4);
  assert.equal(d.minFeaturePx, 1);
  assert.equal(d.materials, true);
  assert.equal(d.style, null);
  assert.equal(rasterOptions({ policy: 'hybrid' }).policy, 2);
  assert.throws(() => rasterOptions({ size: 15 }), RangeError);
  assert.throws(() => rasterOptions({ size: 16385 }), RangeError);
  assert.throws(() => rasterOptions({ size: '512' }), TypeError);
  assert.throws(() => rasterOptions({ policy: 'smooth' }), RangeError);
  assert.throws(() => rasterOptions({ transparent: 1 }), TypeError);
  assert.throws(() => rasterOptions({ minFeaturePx: 0 }), RangeError);
  assert.throws(() => rasterOptions({ supersample: 9 }), RangeError);
  assert.throws(() => rasterOptions({ logo: { width: 2, height: 2, data: new Uint8Array(3) } }), RangeError);
});

test('style specs', () => {
  assert.equal(styleSpec({}), null);
  assert.deepEqual(styleSpec({ preset: 'concept' }), { preset: 1, rgb: [], num: [], logoHref: null });
  const s = styleSpec({ style: { background: '#123', ink: [1, 2, 3], inkShade: { r: 4, g: 5, b: 6 }, policy: 'adaptive', modules: 'circle', logoPlate: true } });
  assert.equal(s.preset, 0);
  assert.deepEqual(s.rgb, [[0, 0x11, 0x22, 0x33], [1, 0x11, 0x22, 0x33], [2, 1, 2, 3], [3, 4, 5, 6]]);
  assert.deepEqual(s.num, [[2, 2], [9, 1], [10, 1]]);
  assert.deepEqual(styleSpec({ style: { accents: ['#ff0000', '#00ff00'] } }).num, [[0, 2]]);
  assert.throws(() => styleSpec({ style: { colour: '#000' } }), TypeError);
  assert.throws(() => styleSpec({ style: { policy: 'lenient' } }), RangeError);
  assert.throws(() => styleSpec({ style: { accents: [1, 2, 3, 4, 5] } }), RangeError);
  assert.throws(() => styleSpec({ style: { bevel: 2 } }), RangeError);
  assert.throws(() => styleSpec({ preset: 'neon' }), RangeError);
  assert.throws(() => parseColour('#12'), RangeError);
  assert.throws(() => parseColour([0, 0, 256]), RangeError);
});

test('svg / pdf / certify options', () => {
  assert.equal(svgOptions().scale, 16);
  assert.equal(svgOptions({ size: 310 }).scale, 10);
  assert.throws(() => svgOptions({ scale: 0 }), RangeError);
  assert.deepEqual(pdfOptions(), { widthPt: 144, transparent: false, style: null });
  assert.throws(() => pdfOptions({ widthPt: 0 }), RangeError);
  const c = certifyOptions({ transparent: true, backdrop: { colours: ['#fff', '#000'] } });
  assert.deepEqual(c.colours, [[255, 255, 255], [0, 0, 0]]);
  assert.equal(c.pxPerModule, -1);
  assert.equal(c.trials, 0);
  assert.throws(() => certifyOptions({ model: { trials: 17 } }), RangeError);
});

test('telemetry mode validation never falls back to 1', () => {
  for (const v of [0, 1, 2]) assert.equal(telemetryMode(v), v);
  for (const v of [3, -1, 1.5, '1', true, null, undefined, NaN, 'off']) {
    assert.throws(() => telemetryMode(v), RangeError, String(v));
  }
});

test('payload bytes and frames', () => {
  assert.deepEqual([...payloadBytes('é')], [0xc3, 0xa9]);
  const src = Uint8Array.of(1, 2, 3, 4);
  const view = payloadBytes(src.subarray(1, 3));
  assert.deepEqual([...view], [2, 3]);
  view[0] = 9;
  assert.equal(src[1], 2, 'payload is copied');
  assert.throws(() => payloadBytes(42), TypeError);
  assert.throws(() => rgbaFrame({ width: 2, height: 2, data: [0] }), TypeError);
  assert.equal(rgbaFrame({ width: 1, height: 1, data: new Uint8ClampedArray(4) }).width, 1);
});
