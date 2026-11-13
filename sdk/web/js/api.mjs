// The CKey Web SDK object, built on an instantiated Emscripten module of the
// C core plus the bridge in sdk/web/src/ckey_web.cpp. Every encode, decode
// and render call goes to the core; this file only marshals data.

import {
  LEVELS,
  LAYOUTS,
  certifyOptions,
  encodeOptions,
  grayFrame,
  payloadBytes,
  pdfOptions,
  rasterOptions,
  rgbaFrame,
  styleSpec,
  svgOptions,
  telemetryMode,
} from './options.mjs';
import { classifyPayload, readableText } from './payload.mjs';
import { TelemetrySender, installBackgroundSend, monotonicNow, utcDay } from './telemetry.mjs';
import { makeScanner } from './scanner.mjs';
import { SafeActions } from './safe-actions.mjs';

const PROFILES = ['0', '0.1', '1', '2', '3']; // indexed by the core's profile code
const ADJUSTMENTS = ['ink', 'accents', 'effects', 'logo', 'background', 'materials'];
const STYLE_POLICY_NAMES = ['strict', 'adaptive', 'override'];
const utf8 = new TextEncoder();
const utf8d = new TextDecoder();

/**
 * @param M        instantiated Emscripten module (exports _ckw_* and HEAPU8)
 * @param options  { fetch, telemetryEndpoint, telemetry, now, day, document, window }
 */
export function createCKey(M, options = {}) {
  if (!M || typeof M._ckw_encode !== 'function') throw new Error('CKey core module is missing its exports');
  if (!M.HEAPU8) throw new Error('CKey core module does not export HEAPU8');
  const now = options.now || monotonicNow;
  const heap = () => M.HEAPU8; // re-read after every call: memory may grow

  // ---- memory helpers ------------------------------------------------------
  const alloc = (n) => {
    const p = M._ckw_malloc(n);
    if (!p) throw new Error('CKey: out of memory');
    return p;
  };
  const copyIn = (bytes) => {
    const p = alloc(bytes.length);
    heap().set(bytes, p);
    return p;
  };
  const cstring = (p) => {
    const h = heap();
    let e = p;
    while (h[e] !== 0) e++;
    return utf8d.decode(h.subarray(p, e));
  };
  const withCString = (s, fn) => {
    const b = utf8.encode(s);
    const p = alloc(b.length + 1);
    heap().set(b, p);
    heap()[p + b.length] = 0;
    try {
      return fn(p);
    } finally {
      M._ckw_free(p);
    }
  };
  const lastError = (fallback) => {
    const m = cstring(M._ckw_last_error());
    return m || fallback;
  };
  /** Copies a malloc'd result of ckw_last_len() bytes out and frees it. */
  const takeOut = (p, what) => {
    if (!p) throw new Error(`CKey: ${lastError(`${what} failed`)}`);
    const n = M._ckw_last_len();
    const out = heap().slice(p, p + n);
    M._ckw_free(p);
    return out;
  };
  const u32s = (p, n) => Array.from(new Uint32Array(heap().buffer, p, n));
  const f64s = (p, n) => Array.from(new Float64Array(heap().buffer, p, n));

  // ---- Safe Actions (verification is the core's) -----------------------------
  const safeActionCall = (bytes, key, nowSeconds, maxLifetime) => {
    if (typeof M._ckw_safe_action !== 'function') throw new Error('this CKey core has no Safe Actions');
    const p = copyIn(bytes);
    const k = key ? copyIn(key) : 0;
    try {
      const out = M._ckw_safe_action(p, bytes.length, k, nowSeconds, maxLifetime);
      if (!out) throw new Error(lastError('not a valid Safe Action'));
      const fields = JSON.parse(cstring(out));
      M._ckw_free(out);
      fields.scope = fields.scope === 1 ? 'cross-device' : 'same-device';
      fields.params = Uint8Array.from((fields.params.match(/../g) || []).map((b) => parseInt(b, 16)));
      return fields;
    } finally {
      M._ckw_free(p);
      if (k) M._ckw_free(k);
    }
  };
  const safeActionCore = {
    parse: (bytes) => safeActionCall(bytes, null, 0, 0),
    verify: (bytes, key, nowSeconds, maxLifetime) => safeActionCall(bytes, key, nowSeconds, maxLifetime),
  };

  // ---- symbols -------------------------------------------------------------
  const registry =
    typeof FinalizationRegistry === 'function' ? new FinalizationRegistry((ptr) => M._ckw_symbol_free(ptr)) : null;

  class CKeySymbol {
    #ptr;
    constructor(ptr) {
      this.#ptr = ptr;
      const info = alloc(20);
      try {
        M._ckw_symbol_info(ptr, info);
        const [version, level, layout, payloadLength] = u32s(info, 5);
        Object.defineProperties(this, {
          version: { value: version, enumerable: true },
          level: { value: LEVELS[level], enumerable: true },
          layout: { value: LAYOUTS[layout], enumerable: true },
          payloadLength: { value: payloadLength, enumerable: true },
          capacity: { value: M._ckw_scalable_capacity(version, level, layout), enumerable: true },
          modules: { value: M._ckw_scalable_modules_across(version), enumerable: true },
        });
      } finally {
        M._ckw_free(info);
      }
      if (registry) registry.register(this, ptr, this);
    }
    /** @internal */
    static ptrOf(sym) {
      if (!(sym instanceof CKeySymbol)) throw new TypeError('expected a symbol from this CKey instance (ckey.encode)');
      if (!sym.#ptr) throw new Error('CKey: this symbol has been freed');
      return sym.#ptr;
    }
    get freed() {
      return !this.#ptr;
    }
    /** Releases the symbol's WebAssembly memory. Safe to call twice. */
    free() {
      if (!this.#ptr) return;
      if (registry) registry.unregister(this);
      M._ckw_symbol_free(this.#ptr);
      this.#ptr = 0;
    }
  }

  // ---- styles --------------------------------------------------------------
  /** Runs fn(stylePtr) with a core style built from a styleSpec (0 = default). */
  const withStyle = (spec, fn) => {
    if (!spec) return fn(0);
    const ws = M._ckw_style_new(spec.preset);
    if (!ws) throw new Error(`CKey: ${lastError('invalid style preset')}`);
    try {
      for (const [field, r, g, b] of spec.rgb) {
        if (!M._ckw_style_set_rgb(ws, field, r, g, b)) throw new RangeError('CKey: invalid style colour');
      }
      for (const [field, v] of spec.num) {
        if (!M._ckw_style_set_num(ws, field, v)) {
          throw new RangeError(
            field === 10 ? 'CKey: this core build supports only the strict style policy' : 'CKey: invalid style value',
          );
        }
      }
      if (spec.logoHref !== null) withCString(spec.logoHref, (p) => M._ckw_style_set_logo(ws, p));
      return fn(ws);
    } finally {
      M._ckw_style_free(ws);
    }
  };

  // ---- rendering -----------------------------------------------------------
  const rasterRGBA = (sym, o) => {
    const ptr = CKeySymbol.ptrOf(sym);
    return withStyle(o.style, (ws) => {
      let logoPtr = 0;
      try {
        if (o.logo) logoPtr = copyIn(o.logo.data.subarray(0, o.logo.width * o.logo.height * 4));
        const p = M._ckw_render_raster(
          ptr, ws, o.size, o.policy, o.transparent ? 1 : 0, o.supersample, o.minFeaturePx, o.materials ? 1 : 0, 0,
          logoPtr, o.logo ? o.logo.width : 0, o.logo ? o.logo.height : 0,
        );
        return takeOut(p, 'render');
      } finally {
        if (logoPtr) M._ckw_free(logoPtr);
      }
    });
  };

  function renderPixels(sym, opts) {
    const o = rasterOptions(opts);
    const px = rasterRGBA(sym, o);
    return { width: o.size, height: o.size, data: new Uint8ClampedArray(px.buffer, px.byteOffset, px.byteLength) };
  }

  function renderToCanvas(sym, canvas, opts) {
    if (!canvas || typeof canvas.getContext !== 'function') {
      throw new TypeError('renderToCanvas needs an HTMLCanvasElement or OffscreenCanvas');
    }
    const img = renderPixels(sym, opts);
    if (canvas.width !== img.width) canvas.width = img.width;
    if (canvas.height !== img.height) canvas.height = img.height;
    const ctx = canvas.getContext('2d');
    if (!ctx) throw new Error('CKey: could not get a 2d context from the canvas');
    let data;
    if (typeof ImageData === 'function') {
      data = new ImageData(img.data, img.width, img.height);
    } else {
      data = ctx.createImageData(img.width, img.height);
      data.data.set(img.data);
    }
    ctx.putImageData(data, 0, 0);
    return canvas;
  }

  function renderPNG(sym, opts) {
    const o = rasterOptions(opts);
    const px = rasterRGBA(sym, o);
    const p = copyIn(px);
    try {
      return takeOut(M._ckw_encode_png_rgba(p, o.size, o.size, o.dpi), 'PNG export');
    } finally {
      M._ckw_free(p);
    }
  }

  function renderSVG(sym, opts) {
    const o = svgOptions(opts);
    const ptr = CKeySymbol.ptrOf(sym);
    return withStyle(o.style, (ws) => utf8d.decode(takeOut(M._ckw_render_svg(ptr, ws, o.scale), 'SVG export')));
  }

  function renderPDF(sym, opts) {
    const o = pdfOptions(opts);
    const ptr = CKeySymbol.ptrOf(sym);
    return withStyle(o.style, (ws) => takeOut(M._ckw_render_pdf(ptr, ws, o.widthPt, o.transparent ? 1 : 0), 'PDF export'));
  }

  // ---- pixel-perfect rendering -------------------------------------------
  // Every grid module is exactly cellsPerModule x cellsPerModule cells and
  // every cell is drawn as pxPerCell x pxPerCell whole pixels, never
  // resampled, so a canvas, a PNG and an SVG of the same symbol are the same
  // image. cellsPerModule 0 (default) is the smallest size that decodes at
  // every version: 3 for the Print layout, 6 for Standard.
  const latticeOptions = (o, what) => {
    o = o || {};
    if (typeof o !== 'object') throw new TypeError(`${what} options must be an object`);
    const cells = o.cellsPerModule === undefined ? 0 : o.cellsPerModule;
    if (!Number.isInteger(cells) || cells < 0 || cells > 32) throw new RangeError('cellsPerModule must be 0..32');
    const px = o.pxPerCell === undefined ? 1 : o.pxPerCell;
    if (!Number.isInteger(px) || px < 1 || px > 64) throw new RangeError('pxPerCell must be an integer 1..64');
    const dpi = o.dpi === undefined ? 0 : o.dpi;
    if (typeof dpi !== 'number' || !(dpi >= 0)) throw new RangeError('dpi must be >= 0');
    return { cells, px, dpi, transparent: !!o.transparent, style: styleSpec({ preset: o.preset, style: o.style }) };
  };
  const latticeCells = (sym, o) => {
    const ptr = CKeySymbol.ptrOf(sym);
    const raw = withStyle(o.style, (ws) => takeOut(M._ckw_render_lattice(ptr, ws, o.cells), 'pixel-perfect render'));
    const side = raw[0] | (raw[1] << 8) | (raw[2] << 16) | (raw[3] << 24);
    const palette = [];
    for (let i = 0; i < 6; i++) palette.push([raw[4 + 3 * i], raw[5 + 3 * i], raw[6 + 3 * i]]);
    return { side, palette, cells: raw.subarray(22, 22 + side * side) };
  };

  /** The lattice itself: { side, palette: [[r,g,b] x6], cells } (0 background, 1 ink, 2+i accent i). */
  function renderLattice(sym, opts) {
    const o = latticeOptions(opts, 'renderLattice');
    const l = latticeCells(sym, o);
    return { side: l.side, palette: l.palette, cells: Uint8Array.from(l.cells) };
  }

  const latticeRGBA = (l, px, transparent) => {
    const w = l.side * px;
    const out = new Uint8ClampedArray(w * w * 4);
    for (let cy = 0; cy < l.side; cy++) {
      for (let cx = 0; cx < l.side; cx++) {
        const v = l.cells[cy * l.side + cx];
        const [r, g, b] = l.palette[v];
        const a = transparent && v === 0 ? 0 : 255;
        for (let y = 0; y < px; y++) {
          let at = ((cy * px + y) * w + cx * px) * 4;
          for (let x = 0; x < px; x++, at += 4) { out[at] = r; out[at + 1] = g; out[at + 2] = b; out[at + 3] = a; }
        }
      }
    }
    return { width: w, height: w, data: out };
  };

  /** Pixel-perfect RGBA pixels: { width, height, data }, side * pxPerCell square. */
  function renderPixelPerfect(sym, opts) {
    const o = latticeOptions(opts, 'renderPixelPerfect');
    return latticeRGBA(latticeCells(sym, o), o.px, o.transparent);
  }

  /** Largest pxPerCell for which the symbol fits in maxDevicePx device pixels (at least 1). */
  function pixelPerfectScale(sym, maxDevicePx, opts) {
    const o = latticeOptions(opts, 'pixelPerfectScale');
    const l = latticeCells(sym, o);
    return Math.max(1, Math.floor(maxDevicePx / l.side));
  }

  const latticeSide = (modules, c) => c * modules + 2 * Math.ceil((c * modules) / 29);
  /**
   * The finest pixel-perfect drawing that fits maxDevicePx: the most cells per
   * module (from the layout's minimum up to 32) at one pixel per cell, then
   * whole pixels per cell beyond 32. Spare pixels buy detail (thin rays, round
   * ring marks), not bigger blocks. { cellsPerModule, pxPerCell, side, devicePx }.
   */
  function pixelPerfectFit(sym, maxDevicePx) {
    const modules = sym.modules;
    let min = 1;
    const defaultSide = latticeCells(sym, latticeOptions({}, 'pixelPerfectFit')).side;
    while (min < 32 && latticeSide(modules, min) < defaultSide) min++;
    let c = min;
    while (c < 32 && latticeSide(modules, c + 1) <= maxDevicePx) c++;
    const side = latticeSide(modules, c);
    const px = c === 32 ? Math.max(1, Math.floor(maxDevicePx / side)) : 1;
    return { cellsPerModule: c, pxPerCell: px, side, devicePx: side * px };
  }

  /**
   * Draws pixel-perfect into a canvas. With cssPx (the box's CSS width) and
   * devicePixelRatio, pxPerCell is chosen as the largest whole number of
   * device pixels per cell that fits; the canvas's CSS size is then set so one
   * canvas pixel is exactly one device pixel, and smoothing is off.
   */
  function renderToCanvasPixelPerfect(sym, canvas, opts) {
    if (!canvas || typeof canvas.getContext !== 'function') {
      throw new TypeError('renderToCanvasPixelPerfect needs an HTMLCanvasElement or OffscreenCanvas');
    }
    const dpr = opts && opts.devicePixelRatio > 0 ? opts.devicePixelRatio : 1;
    let o = latticeOptions(opts, 'renderToCanvasPixelPerfect');
    if (opts && opts.cssPx > 0 && opts.pxPerCell === undefined && opts.cellsPerModule === undefined) {
      const fit = pixelPerfectFit(sym, Math.floor(opts.cssPx * dpr));
      o = { ...o, cells: fit.cellsPerModule, px: fit.pxPerCell };
    }
    const l = latticeCells(sym, o);
    const px = o.px;
    const img = latticeRGBA(l, px, o.transparent);
    if (canvas.width !== img.width) canvas.width = img.width;
    if (canvas.height !== img.height) canvas.height = img.height;
    if (canvas.style) {
      canvas.style.width = img.width / dpr + 'px';
      canvas.style.height = img.height / dpr + 'px';
      canvas.style.imageRendering = 'pixelated';
    }
    const ctx = canvas.getContext('2d');
    if (!ctx) throw new Error('CKey: could not get a 2d context from the canvas');
    ctx.imageSmoothingEnabled = false;
    const data = typeof ImageData === 'function' ? new ImageData(img.data, img.width, img.height) : ctx.createImageData(img.width, img.height);
    if (typeof ImageData !== 'function') data.data.set(img.data);
    ctx.putImageData(data, 0, 0);
    return { canvas, pxPerCell: px, cellsPerModule: o.cells, side: l.side, devicePx: img.width };
  }

  /** Pixel-perfect PNG: each cell exactly pxPerCell x pxPerCell pixels. */
  function renderPNGPixelPerfect(sym, opts) {
    const o = latticeOptions(opts, 'renderPNGPixelPerfect');
    const img = latticeRGBA(latticeCells(sym, o), o.px, o.transparent);
    const p = copyIn(img.data);
    try {
      return takeOut(M._ckw_encode_png_rgba(p, img.width, img.height, o.dpi), 'PNG export');
    } finally {
      M._ckw_free(p);
    }
  }

  /** Pixel-perfect SVG: one rectangle per run of equal cells, crisp edges; pxPerCell sets its pixel size. */
  function renderSVGPixelPerfect(sym, opts) {
    const o = latticeOptions(opts, 'renderSVGPixelPerfect');
    const l = latticeCells(sym, o);
    const hex = ([r, g, b]) => '#' + [r, g, b].map((v) => v.toString(16).padStart(2, '0')).join('');
    const size = l.side * o.px;
    const parts = [`<svg xmlns="http://www.w3.org/2000/svg" width="${size}" height="${size}" viewBox="0 0 ${l.side} ${l.side}" shape-rendering="crispEdges" role="img" aria-label="CKey symbol">`];
    if (!o.transparent) parts.push(`<rect width="${l.side}" height="${l.side}" fill="${hex(l.palette[0])}"/>`);
    for (let colour = 1; colour < 6; colour++) {
      let d = '';
      for (let y = 0; y < l.side; y++) {
        for (let x = 0; x < l.side; ) {
          if (l.cells[y * l.side + x] !== colour) { x++; continue; }
          let end = x + 1;
          while (end < l.side && l.cells[y * l.side + end] === colour) end++;
          d += `M${x} ${y}h${end - x}v1h${x - end}z`;
          x = end;
        }
      }
      if (d) parts.push(`<path fill="${hex(l.palette[colour])}" d="${d}"/>`);
    }
    parts.push('</svg>');
    return parts.join('');
  }

  /** Checks a style against a symbol: { ok, message } (the core's explanation). */
  function checkStyle(sym, opts) {
    const o = rasterOptions({ preset: opts?.preset, style: opts?.style });
    const ptr = CKeySymbol.ptrOf(sym);
    return withStyle(o.style, (ws) => {
      const st = M._ckw_style_check(ptr, ws);
      return { ok: st === 0, message: lastError(st === 0 ? 'style OK' : 'style refused') };
    });
  }

  /** Optical certification through the core's modelled scan (ABI 4 cores). */
  function certify(sym, opts) {
    if (!M._ckw_has_certify || !M._ckw_has_certify()) throw new Error('CKey: this core build has no certification');
    const o = certifyOptions(opts);
    const ptr = CKeySymbol.ptrOf(sym);
    return withStyle(o.style, (ws) => {
      const colours = o.colours.length ? copyIn(Uint8Array.from(o.colours.flat())) : 0;
      const pixels = o.pixels ? copyIn(o.pixels.data.subarray(0, o.pixels.width * o.pixels.height * 4)) : 0;
      const out = alloc(64);
      try {
        const st = M._ckw_certify(
          ptr, ws, o.transparent ? 1 : 0, colours, o.colours.length, pixels,
          o.pixels ? o.pixels.width : 0, o.pixels ? o.pixels.height : 0,
          o.pxPerModule, o.blurPx, o.noise, o.glare, o.trials, out, 0,
        );
        const message = lastError('');
        const [certified, policy, adjustments, trials, trialsPassed, minContrast, featureSeparation, logoLoad] = f64s(out, 8);
        return {
          ok: st === 0,
          certified: certified === 1,
          policy: STYLE_POLICY_NAMES[policy] ?? String(policy),
          adjustments: ADJUSTMENTS.filter((_, i) => adjustments & (1 << i)),
          trials,
          trialsPassed,
          minContrast,
          featureSeparation,
          logoLoad,
          message,
        };
      } finally {
        M._ckw_free(out);
        if (colours) M._ckw_free(colours);
        if (pixels) M._ckw_free(pixels);
      }
    });
  }

  // ---- scanning ------------------------------------------------------------
  let frameBuf = 0;
  let frameCap = 0;
  const frameScratch = (n) => {
    if (n > frameCap) {
      if (frameBuf) M._ckw_free(frameBuf);
      frameBuf = 0;
      frameCap = 0;
      frameBuf = alloc(n);
      frameCap = n;
    }
    return frameBuf;
  };

  const decodeWith = (fnName, f, channels) => {
    const n = f.width * f.height * channels;
    const p = frameScratch(n);
    heap().set(f.data.subarray(0, n), p);
    const info = alloc(16);
    try {
      const out = M[fnName](p, f.width, f.height, 0, info);
      if (!out) return null;
      const bytes = takeOut(out, 'decode');
      const [code, version, layout] = u32s(info, 3);
      return {
        bytes,
        text: readableText(bytes),
        profile: PROFILES[code],
        version: code <= 1 ? version : null,
        layout: code <= 1 ? LAYOUTS[layout] : null,
        profileCode: code,
      };
    } finally {
      M._ckw_free(info);
    }
  };
  const strip = (r) => {
    if (!r) return null;
    const { profileCode, ...rest } = r; // eslint-disable-line no-unused-vars
    return rest;
  };
  const decodeRGBA = (img) => decodeWith('_ckw_decode_rgba', rgbaFrame(img), 4);

  // ---- telemetry -----------------------------------------------------------
  const hasTelemetry = typeof M._ckw_tel_set_mode === 'function';
  const telCore = hasTelemetry && {
    shouldSend: (t) => M._ckw_tel_should_send(t) !== 0,
    takeBatch: (day) =>
      withCString(day, (d) => {
        const p = M._ckw_tel_take_batch(d);
        if (!p) return null;
        const id = M._ckw_tel_last_batch_id();
        return { json: utf8d.decode(takeOut(p, 'telemetry batch')), id };
      }),
    batchResult: (id, status, t) => M._ckw_tel_batch_result(id, status, t) === 0,
  };
  const recorder = hasTelemetry
    ? {
        begin: (t) => M._ckw_tel_session_begin(t),
        frame: (t, ms) => M._ckw_tel_frame(t, ms),
        success: (t, code) => M._ckw_tel_session_success(t, code),
        end: (t) => M._ckw_tel_session_end(t),
      }
    : { begin() {}, frame() {}, success() {}, end() {} };

  const version = cstring(M._ckw_software_version());
  let sender = null;
  if (hasTelemetry) {
    withCString('web', (sdk) =>
      withCString(version, (v) => withCString('web', (platform) => M._ckw_tel_set_client(sdk, v, platform))),
    );
    sender = new TelemetrySender({
      core: telCore,
      fetch: options.fetch === undefined ? undefined : options.fetch,
      endpoint: options.telemetryEndpoint,
      now,
      day: options.day || (() => utcDay()),
    });
    installBackgroundSend(sender, options.document ?? globalThis.document, options.window ?? globalThis.window);
  }

  const setTelemetry = (v) => {
    const mode = telemetryMode(v);
    if (!hasTelemetry) {
      if (mode !== 0) throw new RangeError('this CKey build has no telemetry module; only 0 is accepted');
      return;
    }
    if (M._ckw_tel_set_mode(mode) !== 0) throw new RangeError(`telemetry must be 0, 1 or 2, got ${v}`);
  };
  if (options.telemetry !== undefined) setTelemetry(options.telemetry);

  const ckey = {
    version,
    abiVersion: M._ckw_abi_version(),

    encode(input, opts) {
      const bytes = payloadBytes(input);
      const o = encodeOptions(opts);
      const p = copyIn(bytes);
      try {
        const sym = M._ckw_encode(p, bytes.length, o.version, o.level, o.layout);
        if (!sym) throw new Error(lastError('encode failed'));
        return new CKeySymbol(sym);
      } finally {
        M._ckw_free(p);
      }
    },
    capacity(version, level = 'M', layout = 'standard') {
      const o = encodeOptions({ version, level, layout });
      return M._ckw_scalable_capacity(o.version, o.level, o.layout);
    },

    renderToCanvas,
    renderPixels,
    renderLattice,
    renderPixelPerfect,
    pixelPerfectScale,
    pixelPerfectFit,
    renderToCanvasPixelPerfect,
    renderPNGPixelPerfect,
    renderSVGPixelPerfect,
    renderPNG,
    renderSVG,
    renderPDF,
    checkStyle,
    certify,

    decodeImageData: (img) => strip(decodeRGBA(img)),
    decodeGray: (img) => strip(decodeWith('_ckw_decode_gray', grayFrame(img), 1)),
    classifyPayload,

    createScanner({ onResult, onError } = {}) {
      return makeScanner({ decode: decodeRGBA, telemetry: recorder, now, sender }, { onResult, onError });
    },

    get telemetry() {
      return hasTelemetry ? M._ckw_tel_get_mode() : 0;
    },
    set telemetry(v) {
      setTelemetry(v);
    },
    /** Local aggregate counters (modes 1 and 2; all zero in mode 0). */
    telemetryCounters() {
      if (!hasTelemetry) return null;
      const p = alloc(33 * 8);
      try {
        M._ckw_tel_snapshot(p);
        const v = f64s(p, 33);
        const hist = (o) => ({ count: v[o], sum: v[o + 1], buckets: v.slice(o + 2, o + 12) });
        return {
          sessions: { started: v[0], succeeded: v[1], failed: v[2] },
          frames: v[3],
          decoded: { p0: v[4], p0_1: v[5], p1: v[6], p2: v[7], p3: v[8] },
          time_to_decode_ms: hist(9),
          frame_ms: hist(21),
        };
      } finally {
        M._ckw_free(p);
      }
    },
    /**
     * Safe Actions for this app (spec/CKey-Safe-Actions.md): pin issuer keys with
     * trust(), register actions with register(), and pass decoded text to
     * handle(). Same-device actions run at once; cross-device ones only after
     * request.present() and the person's approval.
     */
    safeActions(appId, safeActionOptions) {
      return new SafeActions(safeActionCore, appId, safeActionOptions);
    },
    /** Attempts a telemetry send now if one is due (mode 1). Never rejects. */
    flushTelemetry() {
      return sender ? sender.trySend() : Promise.resolve(null);
    },
  };
  Object.defineProperty(ckey, '_testing', {
    value: {
      sender,
      resetTelemetry: () => hasTelemetry && M._ckw_tel_reset_for_testing(),
      setClient: () =>
        withCString('web', (sdk) =>
          withCString(version, (v) => withCString('web', (pl) => M._ckw_tel_set_client(sdk, v, pl))),
        ),
    },
  });
  return Object.seal(ckey);
}
