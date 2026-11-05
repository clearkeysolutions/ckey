// Pure-JS tests: telemetry sender scheduling against a fake core that
// mirrors the C core's batch contract (one batch in flight, report once).
import test from 'node:test';
import assert from 'node:assert/strict';
import { TelemetrySender, CKEY_TELEMETRY_ENDPOINT, installBackgroundSend, utcDay } from '../js/telemetry.mjs';

function fakeCore({ due = true } = {}) {
  const core = {
    due,
    nextId: 1,
    inflight: 0,
    results: [],
    takes: [],
    shouldSend(now) {
      core.lastNow = now;
      return core.due && !core.inflight;
    },
    takeBatch(day) {
      if (core.inflight) return null;
      core.inflight = core.nextId++;
      core.takes.push(day);
      return { json: `{"schema":1,"day":"${day}"}`, id: core.inflight };
    },
    batchResult(id, status, now) {
      if (id !== core.inflight) return false;
      core.results.push({ id, status, now });
      core.inflight = 0;
      return true;
    },
  };
  return core;
}

function fakeTimers() {
  const t = { intervals: [], timeouts: [] };
  t.setInterval = (fn, ms) => (t.intervals.push({ fn, ms }), t.intervals.length);
  t.clearInterval = (id) => (t.intervals[id - 1] = null);
  t.setTimeout = (fn) => (t.timeouts.push(fn), t.timeouts.length);
  t.runTimeouts = () => t.timeouts.splice(0).forEach((f) => f());
  return t;
}

test('posts one batch with the documented request shape and reports the status', async () => {
  const core = fakeCore();
  const calls = [];
  let clock = 1000;
  const sender = new TelemetrySender({
    core,
    fetch: async (url, init) => (calls.push({ url, init }), { status: 204 }),
    now: () => clock,
    day: () => '2026-11-05',
    timers: fakeTimers(),
  });
  const p = sender.trySend();
  clock = 1500;
  assert.equal(await p, 204);
  assert.equal(calls.length, 1);
  assert.equal(calls[0].url, CKEY_TELEMETRY_ENDPOINT);
  assert.equal(calls[0].init.method, 'POST');
  assert.equal(calls[0].init.keepalive, true);
  assert.deepEqual(calls[0].init.headers, { 'content-type': 'application/json' });
  assert.equal(calls[0].init.body, '{"schema":1,"day":"2026-11-05"}');
  assert.equal(calls[0].init.credentials, 'omit');
  assert.equal(calls[0].init.referrerPolicy, 'no-referrer');
  assert.deepEqual(core.results, [{ id: 1, status: 204, now: 1500 }]);
  assert.equal(core.lastNow, 1000);
});

test('network errors report status 0 and never reject', async () => {
  const core = fakeCore();
  const sender = new TelemetrySender({ core, fetch: async () => { throw new TypeError('offline'); }, timers: fakeTimers() });
  assert.equal(await sender.trySend(), 0);
  assert.equal(core.results[0].status, 0);
  const odd = new TelemetrySender({ core, fetch: async () => ({}), timers: fakeTimers() });
  assert.equal(await odd.trySend(), 0);
});

test('nothing is sent when the core says no, or without fetch', async () => {
  const core = fakeCore({ due: false });
  let n = 0;
  const sender = new TelemetrySender({ core, fetch: async () => (n++, { status: 204 }), timers: fakeTimers() });
  assert.equal(await sender.trySend(), null);
  assert.equal(n, 0);
  const off = new TelemetrySender({ core: fakeCore(), fetch: null, timers: fakeTimers() });
  assert.equal(await off.trySend(), null);
});

test('concurrent attempts share one in-flight request', async () => {
  const core = fakeCore();
  let release;
  let n = 0;
  const sender = new TelemetrySender({
    core,
    fetch: () => (n++, new Promise((r) => (release = () => r({ status: 429 })))),
    timers: fakeTimers(),
  });
  const a = sender.trySend();
  const b = sender.trySend();
  assert.equal(a, b);
  release();
  assert.equal(await a, 429);
  assert.equal(n, 1);
  assert.equal(core.results.length, 1);
});

test('a stale batch result (mode switched while in flight) is ignored', async () => {
  const core = fakeCore();
  const sender = new TelemetrySender({
    core,
    fetch: async () => { core.inflight = 0; return { status: 204 }; }, // the core dropped it
    timers: fakeTimers(),
  });
  assert.equal(await sender.trySend(), 204);
  assert.deepEqual(core.results, []);
});

test('arm schedules periodic checks once; trySendSoon defers to a later task', async () => {
  const timers = fakeTimers();
  const core = fakeCore();
  let n = 0;
  const sender = new TelemetrySender({ core, fetch: async () => (n++, { status: 204 }), timers, checkIntervalMs: 60000 });
  sender.arm();
  sender.arm();
  assert.equal(timers.intervals.length, 1);
  assert.equal(timers.intervals[0].ms, 60000);
  sender.trySendSoon();
  assert.equal(n, 0, 'never synchronous');
  timers.runTimeouts();
  await sender.inFlight;
  assert.equal(n, 1);
  timers.intervals[0].fn();
  await sender.inFlight;
  assert.equal(n, 2);
  sender.disarm();
  assert.equal(timers.intervals[0], null);
});

test('throwing core never escapes', async () => {
  const core = { shouldSend() { throw new Error('boom'); }, takeBatch() {}, batchResult() {} };
  const sender = new TelemetrySender({ core, fetch: async () => ({ status: 204 }), timers: fakeTimers() });
  assert.equal(await sender.trySend(), null);
});

test('background send on visibilitychange hidden and pagehide', async () => {
  const listeners = {};
  const target = () => ({
    addEventListener: (t, f) => (listeners[t] = f),
    removeEventListener: (t) => delete listeners[t],
  });
  const doc = Object.assign(target(), { visibilityState: 'visible' });
  const win = target();
  let n = 0;
  const sender = { trySend: () => (n++, Promise.resolve(null)) };
  const remove = installBackgroundSend(sender, doc, win);
  listeners.visibilitychange();
  assert.equal(n, 0);
  doc.visibilityState = 'hidden';
  listeners.visibilitychange();
  assert.equal(n, 1);
  listeners.pagehide();
  assert.equal(n, 2);
  remove();
  assert.deepEqual(Object.keys(listeners), []);
  assert.doesNotThrow(() => installBackgroundSend(sender, undefined, undefined)());
});

test('utc day', () => {
  assert.equal(utcDay(new Date('2026-11-05T23:59:59-05:00')), '2026-11-06');
});
