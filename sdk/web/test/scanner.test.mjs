// Pure-JS tests: scanner session logic with a fake decoder and recorder.
import test from 'node:test';
import assert from 'node:assert/strict';
import { makeScanner } from '../js/scanner.mjs';

function setup(decodeAt) {
  const log = [];
  let clock = 0;
  let frames = 0;
  const sender = { armed: 0, soon: 0, arm() { this.armed++; }, trySendSoon() { this.soon++; } };
  const deps = {
    decode: () => {
      clock += 5;
      return ++frames === decodeAt ? { bytes: Uint8Array.of(1), text: null, profile: '0', version: 3, layout: 'standard', profileCode: 0 } : null;
    },
    telemetry: {
      begin: (t) => log.push(['begin', t]),
      frame: (t, ms) => log.push(['frame', t, ms]),
      success: (t, code) => log.push(['success', t, code]),
      end: (t) => log.push(['end', t]),
    },
    now: () => (clock += 1),
    sender,
  };
  return { log, deps, sender };
}

test('begin on first frame, success on decode, end on stop', () => {
  const { log, deps, sender } = setup(2);
  const results = [];
  const s = makeScanner(deps, { onResult: (r) => results.push(r) });
  assert.equal(s.processFrame({}), null);
  const r = s.processFrame({});
  assert.equal(r.profile, '0');
  assert.equal('profileCode' in r, false);
  assert.equal(results.length, 1);
  assert.equal(s.processFrame({}), null, 'frames after the decode are skipped');
  s.stop();
  assert.deepEqual(log.map((e) => e[0]), ['begin', 'frame', 'frame', 'success', 'end']);
  assert.equal(log[1][2], 6, 'processing time is measured around decode');
  assert.equal(log[3][2], 0, 'profile code passed to the core');
  assert.equal(sender.armed, 1);
  assert.equal(sender.soon, 1);
});

test('reset begins a new session; stop without decode ends it', () => {
  const { log, deps } = setup(1);
  const s = makeScanner(deps);
  assert.ok(s.processFrame({}));
  assert.ok(s.decoded);
  s.reset();
  assert.equal(s.decoded, null);
  s.processFrame({});
  s.stop();
  s.stop();
  assert.deepEqual(log.map((e) => e[0]), ['begin', 'frame', 'success', 'begin', 'frame', 'end']);
});

test('decoder errors are counted as frames and reported', () => {
  const { log, deps } = setup(0);
  deps.decode = () => { throw new TypeError('bad frame'); };
  const errors = [];
  const s = makeScanner(deps, { onError: (e) => errors.push(e) });
  assert.equal(s.processFrame({}), null);
  assert.equal(errors.length, 1);
  assert.deepEqual(log.map((e) => e[0]), ['begin', 'frame']);
  const t = makeScanner(deps);
  assert.throws(() => t.processFrame({}), TypeError);
});

test('telemetry failures never break scanning', () => {
  const { deps } = setup(1);
  deps.telemetry = { begin() { throw new Error('x'); }, frame() { throw new Error('x'); }, success() { throw new Error('x'); }, end() { throw new Error('x'); } };
  const s = makeScanner(deps);
  assert.ok(s.processFrame({}));
  assert.doesNotThrow(() => s.stop());
});
