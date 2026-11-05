// CKey renderer page. Uses the CKey web SDK served at /sdk/ckey.mjs.
// Nothing typed here leaves the browser.

import { detectConvention } from './conventions.js';

const $ = (id) => document.getElementById(id);
const els = {
  payload: $('payload'),
  meta: $('payload-meta'),
  convention: $('convention'),
  version: $('version'),
  level: $('level'),
  layout: $('layout'),
  size: $('size'),
  transparent: $('transparent'),
  png: $('dl-png'),
  svg: $('dl-svg'),
  canvas: $('canvas'),
  stage: $('stage'),
  status: $('status'),
};

// Version picker: auto, then 1–31.
{
  const auto = new Option('Auto (smallest that fits)', '0', true, true);
  els.version.add(auto);
  for (let v = 1; v <= 31; v++) els.version.add(new Option(`Version ${v}`, String(v)));
}

const utf8 = new TextEncoder();
let ckey = null;
let sym = null;

function setStatus(text, isError = false) {
  els.status.textContent = text;
  els.status.classList.toggle('error', isError);
}

function setDownloads(enabled) {
  els.png.disabled = !enabled;
  els.svg.disabled = !enabled;
}

function options() {
  let size = Math.round(Number(els.size.value));
  if (!Number.isFinite(size)) size = 512;
  size = Math.min(4096, Math.max(64, size));
  return {
    version: Number(els.version.value),
    level: els.level.value,
    layout: els.layout.value,
    size,
    transparent: els.transparent.checked,
  };
}

function updateHints() {
  const text = els.payload.value;
  const bytes = utf8.encode(text).length;
  els.meta.textContent = `${bytes} byte${bytes === 1 ? '' : 's'} of UTF-8`;
  const c = detectConvention(text);
  if (!c) {
    els.convention.textContent = '';
  } else if (c.kind === 'text') {
    els.convention.textContent = `Detected: ${c.label}.`;
  } else if (c.valid) {
    els.convention.textContent = `Detected: ${c.label}. Readers will offer “${c.action}”.`;
  } else {
    els.convention.textContent = `Looks like ${c.label}, but it ${c.problem}, so readers will show it as plain text.`;
  }
}

function freeSymbol() {
  if (sym) {
    try {
      sym.free();
    } catch {
      /* already freed */
    }
    sym = null;
  }
}

function render() {
  updateHints();
  if (!ckey) return;
  const o = options();
  els.stage.classList.toggle('checker', o.transparent);
  freeSymbol();
  if (els.payload.value === '') {
    els.canvas.classList.add('hidden');
    setDownloads(false);
    setStatus('Type something to make a symbol.');
    return;
  }
  try {
    sym = ckey.encode(els.payload.value, { version: o.version, level: o.level, layout: o.layout });
    ckey.renderToCanvas(sym, els.canvas, { size: o.size, transparent: o.transparent, policy: 'coverage' });
    els.canvas.classList.remove('hidden');
    els.canvas.setAttribute(
      'aria-label',
      `CKey symbol, Profile 0 version ${sym.version}, level ${sym.level}, ${sym.layout} layout`,
    );
    setDownloads(true);
    setStatus(
      `Profile 0 version ${sym.version}, level ${sym.level}, ${sym.layout} layout · ` +
        `${sym.payloadLength} of ${sym.capacity} bytes used`,
    );
  } catch (err) {
    sym = null;
    els.canvas.classList.add('hidden');
    setDownloads(false);
    setStatus(`Cannot make this symbol: ${err && err.message ? err.message : String(err)}`, true);
  }
}

function download(blob, name) {
  const a = document.createElement('a');
  const url = URL.createObjectURL(blob);
  a.href = url;
  a.download = name;
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 10_000);
}

function fileBase() {
  return sym ? `ckey-v${sym.version}-${sym.level}-${sym.layout}` : 'ckey';
}

els.png.addEventListener('click', () => {
  if (!sym) return;
  const o = options();
  try {
    const png = ckey.renderPNG(sym, { size: o.size, transparent: o.transparent });
    download(new Blob([png], { type: 'image/png' }), `${fileBase()}.png`);
  } catch (err) {
    setStatus(`PNG export failed: ${err?.message || err}`, true);
  }
});

els.svg.addEventListener('click', () => {
  if (!sym) return;
  try {
    const svg = ckey.renderSVG(sym);
    download(new Blob([svg], { type: 'image/svg+xml' }), `${fileBase()}.svg`);
  } catch (err) {
    setStatus(`SVG export failed: ${err?.message || err}`, true);
  }
});

let timer = 0;
const schedule = () => {
  clearTimeout(timer);
  timer = setTimeout(render, 120);
};
els.payload.addEventListener('input', schedule);
for (const el of [els.version, els.level, els.layout, els.transparent]) el.addEventListener('change', render);
els.size.addEventListener('change', render);
document.getElementById('controls').addEventListener('submit', (e) => e.preventDefault());

updateHints();

try {
  const mod = await import('/sdk/ckey.mjs');
  if (typeof mod.loadCKey !== 'function') throw new Error('loadCKey is missing from /sdk/ckey.mjs');
  ckey = await mod.loadCKey();
  setStatus('Renderer ready.');
  render();
} catch (err) {
  ckey = null;
  els.canvas.classList.add('hidden');
  setDownloads(false);
  setStatus(
    'The CKey renderer has not been built yet, so no symbol can be drawn. ' +
      'Build the web SDK (sdk/web/dist/ckey.mjs and ckey.wasm) and reload this page.',
    true,
  );
  console.warn('CKey SDK unavailable:', err);
}
