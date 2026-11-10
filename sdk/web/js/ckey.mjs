// CKey Web SDK entry point (npm package `openckey`).
//
//   import { loadCKey } from 'openckey';
//   const ckey = await loadCKey();
//
// loadCKey fetches ckey.wasm from next to this module unless told otherwise.
// The Emscripten glue (ckey-core.mjs) and the WebAssembly binary are built
// from the C core by scripts/build.sh.

import { createCKey } from './api.mjs';

export { classifyPayload, readableText, toHex } from './payload.mjs';
export { CKEY_TELEMETRY_ENDPOINT } from './telemetry.mjs';
export { SAME_DEVICE, CROSS_DEVICE, isSafeAction, SafeActions, SafeActionRequest } from './safe-actions.mjs';

/** Telemetry modes (canonical numeric values shared by every CKey SDK). */
export const TELEMETRY_OFF = 0;
export const TELEMETRY_ON = 1;
export const TELEMETRY_LOCAL = 2;

let shared = null;

/**
 * Loads the WebAssembly core and returns the CKey API object.
 *
 * options:
 *   wasmUrl            URL or string of ckey.wasm (default: next to this module)
 *   wasmBinary         ArrayBuffer/Uint8Array of ckey.wasm (skips fetching)
 *   telemetry          initial mode 0 | 1 | 2 (applied before anything is counted)
 *   fetch              fetch function used for telemetry; null never sends
 *   telemetryEndpoint  override of CKEY_TELEMETRY_ENDPOINT (tests, self-hosting)
 *   moduleFactory      Emscripten factory to use instead of ./ckey-core.mjs
 *
 * Called with no options, every call returns the same instance.
 */
export function loadCKey(options) {
  if (options === undefined) return (shared ??= instantiate({}).catch((e) => ((shared = null), Promise.reject(e))));
  if (options === null || typeof options !== 'object') throw new TypeError('loadCKey options must be an object');
  return instantiate(options);
}

async function instantiate(options) {
  const factory = options.moduleFactory ?? (await import('./ckey-core.mjs')).default;
  const wasmUrl = options.wasmUrl !== undefined ? new URL(String(options.wasmUrl), baseUrl()) : new URL('./ckey.wasm', import.meta.url);
  let wasmBinary = options.wasmBinary;
  if (wasmBinary === undefined) wasmBinary = readLocalWasm(wasmUrl);
  const moduleArgs = {
    locateFile: (path) => (path.endsWith('.wasm') ? wasmUrl.href : path),
    print: () => {},
    printErr: () => {},
  };
  if (wasmBinary !== undefined) moduleArgs.wasmBinary = wasmBinary instanceof Uint8Array ? wasmBinary : new Uint8Array(wasmBinary);
  const M = await factory(moduleArgs);
  return createCKey(M, options);
}

function baseUrl() {
  return typeof location !== 'undefined' && location.href ? location.href : import.meta.url;
}

// In Node (tests, server-side rendering) a file: URL is read directly; the
// browser path fetches and streams the binary instead.
function readLocalWasm(url) {
  if (url.protocol !== 'file:') return undefined;
  const getBuiltin = globalThis.process?.getBuiltinModule;
  if (typeof getBuiltin !== 'function') return undefined; // the glue's own loader handles it
  return getBuiltin('node:fs').readFileSync(url);
}
