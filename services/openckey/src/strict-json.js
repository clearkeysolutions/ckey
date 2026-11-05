// A small, strict JSON parser for untrusted request bodies.
//
// Differences from JSON.parse, all deliberate:
// - duplicate object keys are rejected (JSON.parse silently keeps the last one,
//   which would let a body smuggle a value past a reviewer reading the first);
// - with `integersOnly`, number literals with a fraction or exponent are
//   rejected, so `1.0` and `1e3` are not accepted as integers;
// - nesting depth is bounded;
// - keys such as `__proto__` become ordinary own properties.

export class JSONSyntaxError extends Error {}

const NUMBER_RE = /-?(?:0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?/y;

export function parseStrictJSON(text, { integersOnly = false, maxDepth = 16 } = {}) {
  let i = 0;
  const fail = (msg) => {
    throw new JSONSyntaxError(`${msg} at offset ${i}`);
  };

  const ws = () => {
    while (i < text.length) {
      const c = text.charCodeAt(i);
      if (c === 0x20 || c === 0x09 || c === 0x0a || c === 0x0d) i++;
      else break;
    }
  };

  const literal = (word, val) => {
    if (text.startsWith(word, i)) {
      i += word.length;
      return val;
    }
    return fail('invalid literal');
  };

  const parseString = () => {
    i++; // opening quote
    let out = '';
    let start = i;
    for (;;) {
      if (i >= text.length) fail('unterminated string');
      const c = text.charCodeAt(i);
      if (c === 0x22) {
        out += text.slice(start, i);
        i++;
        return out;
      }
      if (c < 0x20) fail('control character in string');
      if (c === 0x5c) {
        out += text.slice(start, i);
        const e = text[i + 1];
        switch (e) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            const hex = text.slice(i + 2, i + 6);
            if (!/^[0-9a-fA-F]{4}$/.test(hex)) fail('invalid unicode escape');
            out += String.fromCharCode(parseInt(hex, 16));
            i += 4;
            break;
          }
          default: fail('invalid escape');
        }
        i += 2;
        start = i;
        continue;
      }
      i++;
    }
  };

  const parseNumber = () => {
    NUMBER_RE.lastIndex = i;
    const m = NUMBER_RE.exec(text);
    if (!m) fail('invalid number');
    if (integersOnly && (m[1] !== undefined || m[2] !== undefined)) fail('non-integer number');
    i = NUMBER_RE.lastIndex;
    return Number(m[0]);
  };

  const parseValue = (depth) => {
    if (depth > maxDepth) fail('nesting too deep');
    ws();
    const c = text[i];
    if (c === '{') return parseObject(depth + 1);
    if (c === '[') return parseArray(depth + 1);
    if (c === '"') return parseString();
    if (c === 't') return literal('true', true);
    if (c === 'f') return literal('false', false);
    if (c === 'n') return literal('null', null);
    if (c === '-' || (c >= '0' && c <= '9')) return parseNumber();
    return fail('unexpected token');
  };

  const parseObject = (depth) => {
    i++;
    const obj = {};
    const seen = new Set();
    ws();
    if (text[i] === '}') {
      i++;
      return obj;
    }
    for (;;) {
      ws();
      if (text[i] !== '"') fail('expected key');
      const key = parseString();
      if (seen.has(key)) fail('duplicate key');
      seen.add(key);
      ws();
      if (text[i] !== ':') fail("expected ':'");
      i++;
      const val = parseValue(depth);
      Object.defineProperty(obj, key, { value: val, enumerable: true, writable: true, configurable: true });
      ws();
      if (text[i] === ',') { i++; continue; }
      if (text[i] === '}') { i++; return obj; }
      fail("expected ',' or '}'");
    }
  };

  const parseArray = (depth) => {
    i++;
    const arr = [];
    ws();
    if (text[i] === ']') {
      i++;
      return arr;
    }
    for (;;) {
      arr.push(parseValue(depth));
      ws();
      if (text[i] === ',') { i++; continue; }
      if (text[i] === ']') { i++; return arr; }
      fail("expected ',' or ']'");
    }
  };

  const value = parseValue(0);
  ws();
  if (i !== text.length) fail('trailing data');
  return value;
}

const utf8 = new TextDecoder('utf-8', { fatal: true, ignoreBOM: true });

/** Decode a body buffer as strict UTF-8 (no BOM stripping, invalid bytes throw). */
export function decodeUtf8(buf) {
  try {
    return utf8.decode(buf);
  } catch {
    throw new JSONSyntaxError('body is not valid UTF-8');
  }
}
