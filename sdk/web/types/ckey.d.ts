// Type declarations for the CKey Web SDK (npm package `openckey`).
// Hand-written; copied to dist/ckey.d.ts by scripts/assemble.mjs.

export type Level = 'L' | 'M' | 'Q' | 'H';
export type Layout = 'standard' | 'print';
export type RasterPolicy = 'coverage' | 'binary' | 'hybrid';
export type Preset = 'plain' | 'concept';
/** Optical-safety policy carried by a style: strict refuses, adaptive repairs, override draws as given. */
export type StylePolicy = 'strict' | 'adaptive' | 'override';
export type TelemetryMode = 0 | 1 | 2;
export type Profile = '0' | '0.1' | '1' | '2' | '3';
/** '#rgb', '#rrggbb', [r, g, b] or { r, g, b } with 0..255 channels. */
export type Colour = string | readonly [number, number, number] | { r: number; g: number; b: number };

export interface EncodeOptions {
  /** 1..31, or 0 (default) for the smallest version that fits. */
  version?: number;
  /** Error correction level, default 'M'. */
  level?: Level;
  /** Default 'standard'; 'print' is Profile 0.1. */
  layout?: Layout;
}

export interface CKeySymbol {
  readonly version: number;
  readonly level: Level;
  readonly layout: Layout;
  readonly payloadLength: number;
  readonly capacity: number;
  /** Modules across the field (29..232). */
  readonly modules: number;
  readonly freed: boolean;
  /** Releases the symbol's WebAssembly memory. Safe to call twice. */
  free(): void;
}

export interface Style {
  background?: Colour;
  /** Edge of a radial vignette; defaults to `background` (solid). */
  backgroundEdge?: Colour;
  ink?: Colour;
  /** Sheen shade; defaults to `ink` (flat). */
  inkShade?: Colour;
  accents?: Colour[];
  accentPercent?: number;
  modules?: 'rounded' | 'square' | 'circle' | 'diamond';
  rings?: 'joined' | 'dots';
  rays?: 'round' | 'flat';
  bevel?: number;
  shadow?: number;
  texture?: number;
  /** Image URL or data: URI (SVG export). */
  logoHref?: string | null;
  logoRadius?: number;
  logoPlate?: boolean;
  /** Default 'strict'. */
  policy?: StylePolicy;
}

export interface StyleOptions {
  /** Start from a preset (default 'plain': black on white). */
  preset?: Preset;
  /** Overrides applied on top of the preset. */
  style?: Style;
}

export interface ImageLike {
  width: number;
  height: number;
  data: Uint8ClampedArray | Uint8Array;
}

export interface RasterOptions extends StyleOptions {
  /** Pixels across the full 31-unit canvas (16..16384), default 512. */
  size?: number;
  /** Default 'coverage'. */
  policy?: RasterPolicy;
  /** Paint no background: unpainted pixels have alpha 0. Default false. */
  transparent?: boolean;
  /** Samples per pixel axis, 1..8, default 4. */
  supersample?: number;
  /** HYBRID only: minimum mark width in px, default 1. */
  minFeaturePx?: number;
  /** Sheen, depth and grain; default true. */
  materials?: boolean;
  /** Straight-alpha RGBA logo drawn into the style's logo circle. */
  logo?: ImageLike | null;
}

export interface PNGOptions extends RasterOptions {
  /** Records the physical resolution (pHYs) when > 0. */
  dpi?: number;
}

export interface SVGOptions extends StyleOptions {
  /** Pixels per canonical unit (width = 31 * scale), 1..256, default 16. */
  scale?: number;
  /** Alternative to scale: approximate width in px. */
  size?: number;
}

export interface PDFOptions extends StyleOptions {
  /** Page width in points (72 per inch), default 144. */
  widthPt?: number;
  transparent?: boolean;
}

export interface Pixels {
  width: number;
  height: number;
  data: Uint8ClampedArray;
}

export interface DecodeResult {
  bytes: Uint8Array;
  /** The payload as text when it is valid UTF-8 without control characters (tab, LF, CR allowed). */
  text: string | null;
  profile: Profile;
  /** Profile 0 version (1..31); null for Profiles 1-3. */
  version: number | null;
  /** Profile 0 layout; null for Profiles 1-3. */
  layout: Layout | null;
}

export type PayloadKind = 'https' | 'http' | 'mailto' | 'tel' | 'sms' | 'geo' | 'address' | 'vcard' | 'text';

export interface PayloadAction {
  kind: PayloadKind;
  /** What the host hands to the platform if the user accepts; null for text. */
  target: string | null;
  /** The proposed action, stating its target, e.g. "Open openckey.org". */
  label: string;
}

export interface StyleCheck {
  ok: boolean;
  message: string;
}

export interface CertifyOptions extends StyleOptions {
  transparent?: boolean;
  backdrop?: { colours?: Colour[]; pixels?: ImageLike | null };
  model?: { pxPerModule?: number; blurPx?: number; noise?: number; glare?: number; trials?: number };
}

export interface Certification {
  ok: boolean;
  certified: boolean;
  policy: StylePolicy;
  adjustments: Array<'ink' | 'accents' | 'effects' | 'logo' | 'background' | 'materials'>;
  trials: number;
  trialsPassed: number;
  minContrast: number;
  featureSeparation: number;
  logoLoad: number;
  message: string;
}

export interface Histogram {
  count: number;
  sum: number;
  buckets: number[];
}

export interface TelemetryCounters {
  sessions: { started: number; succeeded: number; failed: number };
  frames: number;
  decoded: { p0: number; p0_1: number; p1: number; p2: number; p3: number };
  time_to_decode_ms: Histogram;
  frame_ms: Histogram;
}

export interface Scanner {
  /** Decodes one RGBA frame; returns the result once per session, else null. */
  processFrame(frame: ImageLike | ImageData): DecodeResult | null;
  /** Scan again after a decode. */
  reset(): void;
  /** The host stopped scanning; ends the telemetry session. */
  stop(): void;
  readonly decoded: DecodeResult | null;
  readonly scanning: boolean;
}

export interface CKey {
  readonly version: string;
  readonly abiVersion: number;
  encode(payload: string | ArrayBuffer | ArrayBufferView, options?: EncodeOptions): CKeySymbol;
  capacity(version: number, level?: Level, layout?: Layout): number;
  renderToCanvas<C extends HTMLCanvasElement | OffscreenCanvas>(symbol: CKeySymbol, canvas: C, options?: RasterOptions): C;
  renderPixels(symbol: CKeySymbol, options?: RasterOptions): Pixels;
  renderPNG(symbol: CKeySymbol, options?: PNGOptions): Uint8Array;
  renderSVG(symbol: CKeySymbol, options?: SVGOptions): string;
  renderPDF(symbol: CKeySymbol, options?: PDFOptions): Uint8Array;
  checkStyle(symbol: CKeySymbol, options?: StyleOptions): StyleCheck;
  certify(symbol: CKeySymbol, options?: CertifyOptions): Certification;
  decodeImageData(image: ImageLike | ImageData): DecodeResult | null;
  decodeGray(image: ImageLike): DecodeResult | null;
  classifyPayload(text: string | Uint8Array): PayloadAction;
  createScanner(options?: { onResult?: (result: DecodeResult) => void; onError?: (error: unknown) => void }): Scanner;
  /** 0 off, 1 on (default), 2 local only. Any other value throws RangeError. */
  telemetry: TelemetryMode;
  telemetryCounters(): TelemetryCounters | null;
  flushTelemetry(): Promise<number | null>;
}

export interface LoadOptions {
  wasmUrl?: string | URL;
  wasmBinary?: ArrayBuffer | Uint8Array;
  telemetry?: TelemetryMode;
  fetch?: typeof fetch | null;
  telemetryEndpoint?: string;
  moduleFactory?: (args: object) => Promise<unknown>;
}

export function loadCKey(options?: LoadOptions): Promise<CKey>;
export function classifyPayload(text: string | Uint8Array): PayloadAction;
export function readableText(bytes: Uint8Array): string | null;
export function toHex(bytes: Uint8Array): string;
export const CKEY_TELEMETRY_ENDPOINT: string;
export const TELEMETRY_OFF: 0;
export const TELEMETRY_ON: 1;
export const TELEMETRY_LOCAL: 2;
