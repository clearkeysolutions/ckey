#!/usr/bin/env node
// Writes tests/vectors/safe-actions-v1.json: Safe Action envelopes built and
// signed by an implementation independent of the C core (this file, with
// Node's own Ed25519), each with the verdict a conforming reader must reach.
// The core test (tests/safe_actions_tests.cpp) checks every verdict, so the
// format is pinned by two implementations that agree.
//
//   node tools/safe_actions_vectors.mjs          write
//   node tools/safe_actions_vectors.mjs --check  fail if the file is stale
import { createPrivateKey, createPublicKey, sign } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

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

const json = JSON.stringify({ schema: 1, description: "Safe Action envelopes from an independent implementation, with the verdict a conforming reader must reach. expect: ok, integrity (bad signature), expired (out of time or too long-lived), invalid (malformed).", vectors }, null, 2) + "\n";
if (process.argv.includes("--check")) {
  if (readFileSync(OUT, "utf8") !== json) { console.error("tests/vectors/safe-actions-v1.json is stale; run node tools/safe_actions_vectors.mjs"); process.exit(1); }
  console.log(vectors.length + " Safe Action vectors current");
} else {
  writeFileSync(OUT, json);
  console.log("wrote " + vectors.length + " Safe Action vectors");
}
