#!/usr/bin/env node
// Writes tests/vectors/safe-actions-v1.json: Safe Action envelopes built and
// signed by an implementation independent of the C core (this file, with
// Node's own Ed25519), each with the verdict a conforming reader must reach.
// The core test (tests/safe_actions_tests.cpp) checks every verdict, so the
// format is pinned by two implementations that agree.
//
//   node tools/safe_actions_vectors.mjs          write
//   node tools/safe_actions_vectors.mjs --check  fail if the file is stale
import { createPrivateKey, createPublicKey, sign, verify, createHash } from "node:crypto";
import { existsSync, readFileSync, writeFileSync } from "node:fs";

const OUT = new URL("../tests/vectors/safe-actions-v1.json", import.meta.url);
const CONTEXT = Buffer.concat([Buffer.from("CKey Safe Action v1"), Buffer.from([0])]);
const PREFIX = "ckey-action:1:";
const L = (1n << 252n) + 27742317777372353535851937790883648493n;

function keyFromSeed(seedHex) {
  const seed = Buffer.from(seedHex, "hex");
  const priv = createPrivateKey({ key: Buffer.concat([Buffer.from("302e020100300506032b657004220420", "hex"), seed]), format: "der", type: "pkcs8" });
  const pub = createPublicKey(priv).export({ format: "der", type: "spki" }).subarray(12);
  return { priv, pub };
}

function u8str(s) { const b = Buffer.from(s, "latin1"); return Buffer.concat([Buffer.from([b.length]), b]); }
function u64(n) { const b = Buffer.alloc(8); b.writeBigUInt64BE(BigInt(n)); return b; }

function body(f) {
  const params = Buffer.from(f.params ?? "", "utf8");
  const len = Buffer.alloc(2); len.writeUInt16BE(params.length);
  return Buffer.concat([Buffer.from([f.version ?? 1, f.scope]), u8str(f.issuer), u8str(f.app), u8str(f.action), u8str(f.key_id), u8str(f.request_id), u64(f.issued_at), u64(f.expires_at), len, params]);
}

function envelope(f, key) {
  const b = body(f);
  const sig = sign(null, Buffer.concat([CONTEXT, b]), key.priv);
  return Buffer.concat([b, sig]);
}

const text = (bytes) => PREFIX + bytes.toString("base64url");

// Keys: RFC 8032 test 1 and test 2 seeds.
const ISSUER = keyFromSeed("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
const OTHER = keyFromSeed("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
const NOW = 1794000000; // a fixed "now" for every vector
const base = {
  scope: 0, issuer: "citeory.com", app: "com.viadere", action: "open.library", key_id: "2026-11",
  request_id: "r_8f3K2pQ7xZ", issued_at: NOW - 30, expires_at: NOW + 90, params: "{\"item\":\"apa-7\"}"
};

const vectors = [];
const add = (name, payload, expect, extra = {}) => vectors.push({ name, payload, public_key: (extra.key ?? ISSUER).pub.toString("hex"), now: extra.now ?? NOW, max_lifetime: extra.maxLifetime ?? 600, expect, ...(extra.fields ? { fields: extra.fields } : {}) });

const fieldsOf = (f) => ({ scope: f.scope, issuer: f.issuer, app: f.app, action: f.action, key_id: f.key_id, request_id: f.request_id, issued_at: f.issued_at, expires_at: f.expires_at, params: f.params ?? "" });

add("valid same-device action", text(envelope(base, ISSUER)), "ok", { fields: fieldsOf(base) });
const cross = { ...base, scope: 1, action: "signin.request", request_id: "sess-41fd0a9be2c74", params: "" };
add("valid cross-device sign-in, no params", text(envelope(cross, ISSUER)), "ok", { fields: fieldsOf(cross) });
add("signed by a different key", text(envelope(base, OTHER)), "integrity");
const tampered = envelope(base, ISSUER); tampered[2 + 1 + 11 + 1 + 11 + 1] ^= 0x01; // a byte of the action name
add("action name changed after signing", text(tampered), "integrity");
const swapped = Buffer.from(envelope(base, ISSUER)); const swapAt = 2 + 1 + 11 + 1; swapped.write("com.viaderf", swapAt, "latin1");
add("receiving app changed after signing", text(swapped), "integrity");
add("expired", text(envelope(base, ISSUER)), "expired", { now: NOW + 91 });
add("valid for longer than the app allows", text(envelope({ ...base, expires_at: NOW + 3600 }, ISSUER)), "expired", { maxLifetime: 600 });
add("issued in the future beyond clock tolerance", text(envelope({ ...base, issued_at: NOW + 300, expires_at: NOW + 400 }, ISSUER)), "expired");
add("within clock tolerance", text(envelope({ ...base, issued_at: NOW + 100, expires_at: NOW + 200 }, ISSUER)), "ok", { fields: fieldsOf({ ...base, issued_at: NOW + 100, expires_at: NOW + 200 }) });
add("expires before it is issued", text(envelope({ ...base, issued_at: NOW, expires_at: NOW - 1 }, ISSUER)), "expired");
add("not a Safe Action", "https://citeory.com/", "invalid");
add("wrong version prefix", "ckey-action:2:" + envelope(base, ISSUER).toString("base64url"), "invalid");
add("not base64url", PREFIX + "abc+/def", "invalid");
add("non-canonical base64url tail", PREFIX + envelope(base, ISSUER).toString("base64url").replace(/.$/, (c) => {
  const a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"; return a[(a.indexOf(c) + 1) % 64];
}), "invalid_or_integrity");
add("upper-case issuer", text(envelope({ ...base, issuer: "Citeory.com" }, ISSUER)), "invalid");
add("issuer without a dot", text(envelope({ ...base, issuer: "localhost" }, ISSUER)), "invalid");
add("request ID too short", text(envelope({ ...base, request_id: "abc" }, ISSUER)), "invalid");
add("unknown scope", text(envelope({ ...base, scope: 2 }, ISSUER)), "invalid");
add("unsupported envelope version", text(envelope({ ...base, version: 2 }, ISSUER)), "invalid");
const extra = Buffer.concat([envelope(base, ISSUER), Buffer.from([0])]);
add("extra byte after the signature", text(extra), "invalid");
const truncated = envelope(base, ISSUER).subarray(0, 40);
add("truncated", text(truncated), "invalid");
// S + L encodes the same signature value but is not canonical; RFC 8032 requires rejecting it.
const env = Buffer.from(envelope(base, ISSUER));
const sOff = env.length - 32;
let s = 0n; for (let i = 31; i >= 0; i--) s = (s << 8n) | BigInt(env[sOff + i]);
s += L;
if (s < (1n << 256n)) { for (let i = 0; i < 32; i++) { env[sOff + i] = Number(s & 0xffn); s >>= 8n; } add("non-canonical signature scalar (S + L)", text(env), "integrity"); }

// ---- ES256 (ECDSA P-256 over SHA-256, raw r || s, low-S) ----
// Node's ECDSA uses a random nonce, so these vectors cannot be regenerated byte
// for byte: they are written once (or with --es256) and --check verifies every
// verdict with Node's own P-256 instead of comparing bytes.
const OUT_ES256 = new URL("../tests/vectors/safe-actions-es256-v1.json", import.meta.url);
const N256 = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551n;
function p256FromScalar(dHex) {
  // SEC 1 ECPrivateKey with only the scalar; OpenSSL derives the public point.
  const der = Buffer.concat([Buffer.from("30310201010420", "hex"), Buffer.from(dHex, "hex"), Buffer.from("a00a06082a8648ce3d030107", "hex")]);
  const priv = createPrivateKey({ key: der, format: "der", type: "sec1" });
  const jwk = createPublicKey(priv).export({ format: "jwk" });
  const x = Buffer.from(jwk.x, "base64url"), y = Buffer.from(jwk.y, "base64url");
  return { priv, pub: createPublicKey(priv), uncompressed: Buffer.concat([Buffer.from([4]), x, y]), compressed: Buffer.concat([Buffer.from([2 + (y[31] & 1)]), x]) };
}
const be32 = (v) => Buffer.from(v.toString(16).padStart(64, "0"), "hex");
function es256(f, key, { highS = false } = {}) {
  const b = body(f);
  const raw = sign("sha256", Buffer.concat([CONTEXT, b]), { key: key.priv, dsaEncoding: "ieee-p1363" });
  let sv = BigInt("0x" + raw.subarray(32).toString("hex"));
  if (sv > N256 / 2n) sv = N256 - sv;   // low-S, as issuers must send
  if (highS) sv = N256 - sv;
  return Buffer.concat([b, raw.subarray(0, 32), be32(sv)]);
}
// RFC 6979 A.2.5's P-256 key, and a second one.
const P1 = p256FromScalar("c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721");
const P2 = p256FromScalar("0f56db78ca460b055c500064824bed999a25aaf48ebb519ac201537b85479813");
const es = [];
const addEs = (name, payload, expect, key, extra = {}) => es.push({ name, alg: "ES256", payload, public_key: key.toString("hex"), now: extra.now ?? NOW, max_lifetime: extra.maxLifetime ?? 600, expect, ...(extra.fields ? { fields: extra.fields } : {}) });
const esBase = { ...base, key_id: "vault-p256-1" };
addEs("ES256 valid, uncompressed key", text(es256(esBase, P1)), "ok", P1.uncompressed, { fields: fieldsOf(esBase) });
const esCross = { ...cross, key_id: "vault-p256-1" };
addEs("ES256 valid cross-device, compressed key", text(es256(esCross, P1)), "ok", P1.compressed, { fields: fieldsOf(esCross) });
addEs("ES256 high-S form of a valid signature", text(es256(esBase, P1, { highS: true })), "integrity", P1.uncompressed);
addEs("ES256 signed by a different key", text(es256(esBase, P2)), "integrity", P1.compressed);
const esTampered = es256(esBase, P1); esTampered[2 + 1 + 11 + 1 + 11 + 1] ^= 0x01;
addEs("ES256 action name changed after signing", text(esTampered), "integrity", P1.uncompressed);
addEs("ES256 expired", text(es256(esBase, P1)), "expired", P1.uncompressed, { now: NOW + 91 });
addEs("Ed25519 envelope offered an ES256 key", text(envelope(base, ISSUER)), "integrity", P1.uncompressed);
const offCurve = Buffer.from(P1.uncompressed); offCurve[64] ^= 1;
addEs("ES256 key not on the curve", text(es256(esBase, P1)), "invalid", offCurve);
addEs("ES256 key of the wrong length", text(es256(esBase, P1)), "invalid", P1.uncompressed.subarray(0, 64));
const esJson = () => JSON.stringify({ schema: 1, description: "ES256 Safe Action envelopes signed by Node's P-256 (random nonces, so written once), with the verdict a conforming reader must reach.", vectors: es }, null, 2) + "\n";

// Checks an ES256 vector file's verdicts with Node's own P-256.
function checkEs256(file) {
  const doc = JSON.parse(readFileSync(file, "utf8"));
  let ok = 0;
  for (const v of doc.vectors) {
    const env = Buffer.from(v.payload.slice(PREFIX.length), "base64url");
    const key = Buffer.from(v.public_key, "hex");
    let verdict;
    try {
      // SubjectPublicKeyInfo for a P-256 point, compressed (33 bytes) or not (65).
      const spki = Buffer.concat([Buffer.from(key.length === 33 ? "3039301306072a8648ce3d020106082a8648ce3d030107032200" : "3059301306072a8648ce3d020106082a8648ce3d030107034200", "hex"), key]);
      if (key.length !== 33 && key.length !== 65) throw new Error("length");
      const pub = createPublicKey({ key: spki, format: "der", type: "spki" });
      const sig = env.subarray(env.length - 64), msg = Buffer.concat([CONTEXT, env.subarray(0, env.length - 64)]);
      const low = BigInt("0x" + sig.subarray(32).toString("hex")) <= N256 / 2n;
      verdict = verify("sha256", msg, { key: pub, dsaEncoding: "ieee-p1363" }, sig) && low ? "signed" : "integrity";
    } catch { verdict = "invalid"; }
    const expect = v.expect === "ok" || v.expect === "expired" ? "signed" : v.expect;
    if (verdict === expect) ok++; else console.error("ES256 vector \"" + v.name + "\": Node says " + verdict + ", file says " + v.expect);
  }
  if (ok !== doc.vectors.length) process.exit(1);
  return ok;
}

const json = JSON.stringify({ schema: 1, description: "Safe Action envelopes from an independent implementation, with the verdict a conforming reader must reach. expect: ok, integrity (bad signature), expired (out of time or too long-lived), invalid (malformed).", vectors }, null, 2) + "\n";
if (process.argv.includes("--check")) {
  if (readFileSync(OUT, "utf8") !== json) { console.error("tests/vectors/safe-actions-v1.json is stale; run node tools/safe_actions_vectors.mjs"); process.exit(1); }
  console.log(vectors.length + " Safe Action vectors current; " + checkEs256(OUT_ES256) + " ES256 vectors verified");
} else {
  writeFileSync(OUT, json);
  if (!existsSync(OUT_ES256) || process.argv.includes("--es256")) writeFileSync(OUT_ES256, esJson());
  console.log("wrote " + vectors.length + " Safe Action vectors; " + checkEs256(OUT_ES256) + " ES256 vectors verified");
}
