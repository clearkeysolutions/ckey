// Option validation for the CKey Web SDK. Pure JavaScript (no WebAssembly),
// so it is unit-tested without a build. Wrong types throw TypeError and
// out-of-range values throw RangeError; nothing is silently replaced.

// Indexed by the core's codes (ckey_ecc_level, ckey_scalable_layout). U, X and
// the grouped layouts need readers from 0.1.0-build.86 on, so they are opt-in.
export const LEVELS = Object.freeze(['L', 'M', 'Q', 'H', 'U', 'X']);
export const LAYOUTS = Object.freeze(['standard', 'print', 'standard-grouped', 'print-grouped', 'pixel']);
export const RASTER_POLICIES = Object.freeze(['coverage', 'binary', 'hybrid']);
export const PRESETS = Object.freeze(['plain', 'concept']);
export const STYLE_POLICIES = Object.freeze(['strict', 'adaptive', 'override']);
export const MODULE_SHAPES = Object.freeze(['rounded', 'square', 'circle', 'diamond']);
export const RING_STYLES = Object.freeze(['joined', 'dots']);
export const RAY_CAPS = Object.freeze(['round', 'flat']);

const isObj = (v) => v !== null && typeof v === 'object';

function opts(o, what) {
  if (o === undefined || o === null) return {};
  if (!isObj(o)) throw new TypeError(`${what} options must be an object`);
  return o;
}

function int(o, key, def, min, max) {
  const v = o[key];
  if (v === undefined) return def;
  if (typeof v !== 'number' || !Number.isInteger(v)) throw new TypeError(`${key} must be an integer`);
  if (v < min || v > max) throw new RangeError(`${key} must be ${min}..${max}, got ${v}`);
  return v;
}

function num(o, key, def, min, max, { exclusiveMin = false } = {}) {
  const v = o[key];
  if (v === undefined) return def;
  if (typeof v !== 'number' || !Number.isFinite(v)) throw new TypeError(`${key} must be a finite number`);
  if ((exclusiveMin ? v <= min : v < min) || v > max) {
    throw new RangeError(`${key} must be ${exclusiveMin ? '>' : '>='} ${min} and <= ${max}, got ${v}`);
  }
  return v;
}

function bool(o, key, def) {
  const v = o[key];
  if (v === undefined) return def;
  if (typeof v !== 'boolean') throw new TypeError(`${key} must be a boolean`);
  return v;
}

function oneOf(o, key, list, def) {
  const v = o[key];
  if (v === undefined) return def;
  if (typeof v !== 'string') throw new TypeError(`${key} must be one of ${list.join(', ')}`);
  const i = list.indexOf(key === 'level' ? v.toUpperCase() : v.toLowerCase());
  if (i < 0) throw new RangeError(`${key} must be one of ${list.join(', ')}, got ${JSON.stringify(v)}`);
  return i;
}

/** Payload to bytes: a string is UTF-8 encoded; bytes are copied. */
export function payloadBytes(input) {
  if (typeof input === 'string') return new TextEncoder().encode(input);
  if (input instanceof ArrayBuffer) return new Uint8Array(input.slice(0));
  if (ArrayBuffer.isView(input)) return new Uint8Array(input.buffer.slice(input.byteOffset, input.byteOffset + input.byteLength));
  throw new TypeError('payload must be a string, ArrayBuffer or typed array');
}

/** { version 0..31 (0 = auto), level 'L'|'M'|'Q'|'H'|'U'|'X', layout 'standard'|'print'|'standard-grouped'|'print-grouped'|'pixel' } -> numeric codes. */
export function encodeOptions(o) {
  o = opts(o, 'encode');
  return {
    version: int(o, 'version', 0, 0, 31),
    level: oneOf(o, 'level', LEVELS, 1),
    layout: oneOf(o, 'layout', LAYOUTS, 0),
  };
}

/** Colour: '#rgb', '#rrggbb', [r, g, b] or { r, g, b } with 0..255 channels. */
export function parseColour(v, key = 'colour') {
  let c;
  if (typeof v === 'string') {
    const m = /^#?([0-9a-f]{3}|[0-9a-f]{6})$/i.exec(v.trim());
    if (!m) throw new RangeError(`${key} must be #rgb or #rrggbb, got ${JSON.stringify(v)}`);
    const h = m[1].length === 3 ? [...m[1]].map((x) => x + x).join('') : m[1];
    c = [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16));
  } else if (Array.isArray(v) && v.length === 3) {
    c = v;
  } else if (isObj(v) && 'r' in v && 'g' in v && 'b' in v) {
    c = [v.r, v.g, v.b];
  } else {
    throw new TypeError(`${key} must be '#rrggbb', [r, g, b] or {r, g, b}`);
  }
  for (const x of c) {
    if (typeof x !== 'number' || !Number.isInteger(x) || x < 0 || x > 255) {
      throw new RangeError(`${key} channels must be integers 0..255`);
    }
  }
  return c;
}

/**
 * Style options -> a plain description the wrapper applies to a core style.
 * Returns null when neither a preset nor a style is given (the core default:
 * black on white). Range limits on effects are the core style check's job,
 * which explains any refusal.
 *
 * Setting `background` without `backgroundEdge` makes the background solid;
 * setting `ink` without `inkShade` makes the ink flat.
 */
export function styleSpec(o) {
  o = opts(o, 'render');
  const hasPreset = o.preset !== undefined;
  const preset = oneOf(o, 'preset', PRESETS, 0);
  if (o.style === undefined || o.style === null) return hasPreset ? { preset, rgb: [], num: [], logoHref: null } : null;
  const s = o.style;
  if (!isObj(s)) throw new TypeError('style must be an object');
  const known = new Set([
    'background', 'backgroundEdge', 'ink', 'inkShade', 'accents', 'accentPercent', 'modules', 'rings', 'rays',
    'bevel', 'shadow', 'texture', 'logoHref', 'logoRadius', 'logoPlate', 'policy',
  ]);
  for (const k of Object.keys(s)) if (!known.has(k)) throw new TypeError(`unknown style option ${JSON.stringify(k)}`);
  const rgb = []; // [field, r, g, b]  0 background, 1 edge, 2 ink, 3 shade, 4..7 accents
  const numf = []; // [field, value]   see ckw_style_set_num
  if (s.background !== undefined) {
    const c = parseColour(s.background, 'background');
    rgb.push([0, ...c]);
    if (s.backgroundEdge === undefined) rgb.push([1, ...c]);
  }
  if (s.backgroundEdge !== undefined) rgb.push([1, ...parseColour(s.backgroundEdge, 'backgroundEdge')]);
  if (s.ink !== undefined) {
    const c = parseColour(s.ink, 'ink');
    rgb.push([2, ...c]);
    if (s.inkShade === undefined) rgb.push([3, ...c]);
  }
  if (s.inkShade !== undefined) rgb.push([3, ...parseColour(s.inkShade, 'inkShade')]);
  if (s.accents !== undefined) {
    if (!Array.isArray(s.accents) || s.accents.length > 4) throw new RangeError('accents must be an array of at most 4 colours');
    s.accents.forEach((a, i) => rgb.push([4 + i, ...parseColour(a, `accents[${i}]`)]));
    numf.push([0, s.accents.length]);
  }
  if (s.accentPercent !== undefined) numf.push([1, int(s, 'accentPercent', 0, 0, 100)]);
  if (s.modules !== undefined) numf.push([2, oneOf(s, 'modules', MODULE_SHAPES, 0)]);
  if (s.rings !== undefined) numf.push([3, oneOf(s, 'rings', RING_STYLES, 0)]);
  if (s.rays !== undefined) numf.push([4, oneOf(s, 'rays', RAY_CAPS, 0)]);
  if (s.bevel !== undefined) numf.push([5, num(s, 'bevel', 0, 0, 1)]);
  if (s.shadow !== undefined) numf.push([6, num(s, 'shadow', 0, 0, 1)]);
  if (s.texture !== undefined) numf.push([7, num(s, 'texture', 0, 0, 1)]);
  if (s.logoRadius !== undefined) numf.push([8, num(s, 'logoRadius', 0, 0, 29)]);
  if (s.logoPlate !== undefined) numf.push([9, bool(s, 'logoPlate', false) ? 1 : 0]);
  if (s.policy !== undefined) numf.push([10, oneOf(s, 'policy', STYLE_POLICIES, 0)]);
  let logoHref = null;
  if (s.logoHref !== undefined && s.logoHref !== null) {
    if (typeof s.logoHref !== 'string') throw new TypeError('logoHref must be a string');
    logoHref = s.logoHref;
  }
  return { preset, rgb, num: numf, logoHref };
}

function imageLike(v, key, channels) {
  if (!isObj(v)) throw new TypeError(`${key} must be { width, height, data }`);
  const { width, height, data } = v;
  if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0) {
    throw new RangeError(`${key} width and height must be positive integers`);
  }
  if (!(data instanceof Uint8Array || data instanceof Uint8ClampedArray)) {
    throw new TypeError(`${key}.data must be a Uint8Array or Uint8ClampedArray`);
  }
  if (data.length < width * height * channels) throw new RangeError(`${key}.data is too short for ${width}x${height}`);
  return { width, height, data };
}

/** Raster options for renderPixels / renderToCanvas / renderPNG. */
export function rasterOptions(o) {
  o = opts(o, 'render');
  return {
    size: int(o, 'size', 512, 16, 16384),
    policy: oneOf(o, 'policy', RASTER_POLICIES, 0),
    transparent: bool(o, 'transparent', false),
    supersample: int(o, 'supersample', 4, 1, 8),
    minFeaturePx: num(o, 'minFeaturePx', 1, 0, 1024, { exclusiveMin: true }),
    materials: bool(o, 'materials', true),
    dpi: num(o, 'dpi', 0, 0, 100000),
    logo: o.logo === undefined || o.logo === null ? null : imageLike(o.logo, 'logo', 4),
    style: styleSpec(o),
  };
}

/** SVG options: scale (px per canonical unit, 1..256, default 16) or size (px, -> scale). */
export function svgOptions(o) {
  o = opts(o, 'renderSVG');
  let scale = int(o, 'scale', 16, 1, 256);
  if (o.scale === undefined && o.size !== undefined) {
    const size = int(o, 'size', 496, 31, 31 * 256);
    scale = Math.max(1, Math.round(size / 31));
  }
  return { scale, style: styleSpec(o) };
}

/** PDF options: widthPt (points, 72 per inch; default 144 = 2 in) and transparent. */
export function pdfOptions(o) {
  o = opts(o, 'renderPDF');
  return {
    widthPt: num(o, 'widthPt', 144, 0, 14400, { exclusiveMin: true }),
    transparent: bool(o, 'transparent', false),
    style: styleSpec(o),
  };
}

/** Certification options (ckey.certify). */
export function certifyOptions(o) {
  o = opts(o, 'certify');
  const backdrop = o.backdrop === undefined || o.backdrop === null ? {} : o.backdrop;
  if (!isObj(backdrop)) throw new TypeError('backdrop must be an object');
  const colours = backdrop.colours === undefined ? [] : backdrop.colours;
  if (!Array.isArray(colours) || colours.length > 16) throw new RangeError('backdrop.colours must be an array of at most 16 colours');
  const model = o.model === undefined || o.model === null ? {} : o.model;
  if (!isObj(model)) throw new TypeError('model must be an object');
  return {
    transparent: bool(o, 'transparent', false),
    colours: colours.map((c, i) => parseColour(c, `backdrop.colours[${i}]`)),
    pixels: backdrop.pixels === undefined || backdrop.pixels === null ? null : imageLike(backdrop.pixels, 'backdrop.pixels', 4),
    pxPerModule: num(model, 'pxPerModule', -1, 0, 1000, { exclusiveMin: model.pxPerModule !== undefined }),
    blurPx: num(model, 'blurPx', -1, 0, 100),
    noise: num(model, 'noise', -1, 0, 1),
    glare: num(model, 'glare', -1, 0, 0.9),
    trials: int(model, 'trials', 0, 1, 16),
    style: styleSpec(o),
  };
}

/** RGBA frame for decodeImageData: ImageData or { width, height, data }. */
export function rgbaFrame(v) {
  return imageLike(v, 'image', 4);
}

/** 8-bit grayscale frame for decodeGray: { width, height, data } with width * height bytes. */
export function grayFrame(v) {
  return imageLike(v, 'image', 1);
}

/** Telemetry mode: exactly 0, 1 or 2. Anything else is a RangeError (never silently 1). */
export function telemetryMode(v) {
  if (v === 0 || v === 1 || v === 2) return v;
  throw new RangeError(`telemetry must be 0 (off), 1 (on) or 2 (local only), got ${typeof v === 'string' ? JSON.stringify(v) : String(v)}`);
}
