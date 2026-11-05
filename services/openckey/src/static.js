// Static files: the renderer page (public/) and the web SDK (OPENCKEY_SDK_DIR).

import { readFile } from 'node:fs/promises';
import { extname, join, resolve, sep } from 'node:path';
import { HttpError, sendBody } from './http.js';

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.map': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.wasm': 'application/wasm',
  '.txt': 'text/plain; charset=utf-8',
};

const NAME_RE = /^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/;

/** CSP for the renderer page: own scripts, WebAssembly, canvas downloads. */
export const RENDERER_CSP = [
  "default-src 'none'",
  "script-src 'self' 'wasm-unsafe-eval'",
  "style-src 'self'",
  "img-src 'self' data: blob:",
  "connect-src 'self'",
  "base-uri 'none'",
  "form-action 'none'",
  "frame-ancestors 'none'",
].join('; ');

/** Read `name` from `dir`, refusing anything that could escape it. */
async function readSafe(dir, name, allowedExts) {
  if (!NAME_RE.test(name) || name.includes('..')) throw new HttpError(404, 'not found');
  const ext = extname(name).toLowerCase();
  if (!allowedExts.has(ext)) throw new HttpError(404, 'not found');
  const root = resolve(dir);
  const path = resolve(join(root, name));
  if (!path.startsWith(root + sep)) throw new HttpError(404, 'not found');
  try {
    return { data: await readFile(path), type: MIME[ext] };
  } catch {
    return null;
  }
}

const ASSET_EXTS = new Set(['.html', '.css', '.js', '.mjs', '.svg', '.png', '.ico', '.txt', '.json']);
const SDK_EXTS = new Set(['.mjs', '.js', '.wasm', '.map', '.json']);

export function makeStaticHandlers({ config }) {
  async function index(req, res) {
    const f = await readSafe(config.publicDir, 'index.html', ASSET_EXTS);
    if (!f) throw new HttpError(500, 'renderer page missing');
    sendBody(res, 200, f.type, f.data, { 'Cache-Control': 'no-cache', 'Content-Security-Policy': RENDERER_CSP });
  }

  async function asset(req, res, { params }) {
    const f = await readSafe(join(config.publicDir, 'assets'), params.file, ASSET_EXTS);
    if (!f) throw new HttpError(404, 'not found');
    sendBody(res, 200, f.type, f.data, { 'Cache-Control': 'public, max-age=300' });
  }

  async function sdk(req, res, { params }) {
    const f = await readSafe(config.sdkDir, params.file, SDK_EXTS);
    if (!f) throw new HttpError(404, 'SDK file not found (the web SDK has not been built)');
    sendBody(res, 200, f.type, f.data, { 'Cache-Control': 'no-cache' });
  }

  function robots(req, res) {
    sendBody(res, 200, MIME['.txt'], 'User-agent: *\nDisallow: /r/\nDisallow: /t/\nDisallow: /api/\n', {
      'Cache-Control': 'public, max-age=86400',
    });
  }

  return { index, asset, sdk, robots };
}
