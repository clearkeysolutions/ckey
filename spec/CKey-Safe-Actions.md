# CKey Safe Actions (draft 1)

A **Safe Action** is a code that a CKey reader may act on **without asking first**. It is the one exception to the rule that a reader shows what a code holds and acts only on the user's approval ([payload conventions](CKey-Payload-Conventions.md), rule 1).

The exception is narrow by design. A Safe Action is a signed request from a known site (the **issuer**) to a known app (the **receiving app**) to run one action that app has registered. The receiving app's SDK verifies it before anything happens, and anything that does not verify is ordinary text.

Example: on `citeory.com`, a "Sign in with Viadere" code carries a Safe Action for the app `com.viadere`, action `signin.request`, signed by Citeory's key. Viadere's app verifies it and starts the sign-in.

## What CKey does and does not vouch for

The format, the verification rules and the official SDKs make sure an action is **genuine**: it came from the issuer it names, for the app and action it names, unchanged, once, and in time. CKey never vouches for **what** an action does. That is the issuer's and the receiving app's responsibility. openckey.org certifies how a code looks and scans, never its content ([trademark policy](https://openckey.org/trademark)).

## Terms

- **Issuer:** the site that creates and signs Safe Actions, named by its DNS name, for example `citeory.com`. It holds a signing key, Ed25519 on its own server or ES256 (ECDSA P-256) in a key service, and never shares it.
- **Receiving app:** the app that runs the action, named by an app ID such as `com.viadere`. It registers which actions it accepts, from which issuers, and with which scope.
- **Scope:**
  - **same-device:** the result stays on the device that scanned, inside the receiving app. Examples: open a screen, look up an item, add something to a list.
  - **cross-device:** the action approves something somewhere else, for example signing in a web browser on another computer.

## Text form

A Safe Action payload is UTF-8 text:

```
ckey-action:1:<envelope>
```

`<envelope>` is the envelope's bytes in base64url (RFC 4648 section 5) **without padding**, and any unused low bits of the final character are zero. A reader that does not support Safe Actions sees text that is not a link, so it shows it as text and nothing happens.

## Envelope

All integers are big-endian.

| Field | Encoding | Rules |
|---|---|---|
| version | u8 | `1` |
| scope | u8 | `0` same-device, `1` cross-device |
| issuer | u8 length + bytes | DNS name, lower case `a-z 0-9 - .`, at least one dot, 1 to 253 bytes |
| app | u8 length + bytes | `A-Z a-z 0-9 . _ -`, 1 to 255 bytes |
| action | u8 length + bytes | `a-z 0-9 . _ -`, 1 to 64 bytes |
| key_id | u8 length + bytes | `A-Z a-z 0-9 . _ -`, 1 to 32 bytes |
| request_id | u8 length + bytes | `A-Z a-z 0-9 _ -`, 8 to 64 bytes, single use |
| issued_at | u64 | Unix seconds |
| expires_at | u64 | Unix seconds, later than `issued_at` |
| params | u16 length + bytes | at most 1024 bytes; UTF-8 JSON is recommended |
| signature | 64 bytes | Ed25519 (RFC 8032), or ES256: ECDSA P-256 raw `r ‖ s` (see [issuer keys](#issuer-keys)) |

The signature covers the **signing input**: the ASCII text `CKey Safe Action v1`, one zero byte, then every envelope byte before the signature. Nothing may follow the signature. The envelope does not name the algorithm; the signing key does (its `alg` in the issuer's key file, or in the receiving app's pinned keys):

- **Ed25519** (RFC 8032) over the signing input. Verifiers reject a signature whose scalar S is not below the group order (RFC 8032 section 5.1.7).
- **ES256**: ECDSA over the NIST P-256 curve (FIPS 186-5) with SHA-256 of the signing input. The 64 bytes are `r ‖ s`, each a 32-byte big-endian integer with 1 ≤ r, s < n. **s must be in the low half (s ≤ n/2)**; verifiers reject a high-S signature, so each envelope has exactly one valid signature value. Issuers whose key service returns a DER signature, or a high-S one, convert it (`ckey_es256_signature_from_der` does both).

## Issuer keys

An issuer publishes its public keys at:

```
https://<issuer>/.well-known/ckey-safe-actions.json
```

```json
{
  "issuer": "citeory.com",
  "keys": [
    { "id": "2026-11", "alg": "Ed25519", "public_key": "<64 hex digits>", "not_after": "2027-11-01T00:00:00Z" },
    { "id": "vault-p256-1", "alg": "ES256", "public_key": "<66 or 130 hex digits>", "not_after": "2027-11-01T00:00:00Z" }
  ],
  "request_info": "https://citeory.com/api/ckey/requests/{request_id}"
}
```

- `alg` is `Ed25519` (`public_key`: the 32-byte key, 64 hex digits) or `ES256` (`public_key`: the SEC 1 encoding of the P-256 point, 33 bytes compressed (`02`/`03` prefix, 66 hex digits) or 65 bytes uncompressed (`04` prefix, 130 hex digits)). A reader ignores keys with any other `alg`, and a key whose point is not on P-256 is invalid. An issuer may publish keys of both algorithms; each Safe Action is verified with the key its `key_id` names.
- A key is bound to the issuer by where it is published: only whoever controls `citeory.com` can publish keys for it.
- A receiving app may instead **pin** keys in its own code. Pinned keys are preferred for sign-in and payments.
- To rotate a key, publish the new one beside the old, start signing with it, then remove the old one. To revoke a compromised key, remove it at once. Readers cache the file for at most one hour.
- `request_info` is required for cross-device actions (below).

## Request information (cross-device)

For a cross-device action the reader shows a confirmation, and that confirmation must say **who is asking**. The reader fetches `request_info` with `{request_id}` replaced, over HTTPS, from the issuer's own domain:

```json
{
  "request_id": "sess-41fd0a9be2c74",
  "device": "Chrome on Windows",
  "location": "near Lagos, Nigeria",
  "requested_at": "2026-11-07T20:14:03Z",
  "status": "pending"
}
```

If it cannot be fetched, or its `status` is not `pending`, the action does not run.

## What a reader must do

A reader built on an official CKey SDK does exactly this; any other reader claiming Safe Actions support must do the same ([conformance](#conformance)).

1. A payload that does not start with `ckey-action:1:` follows the payload conventions unchanged.
2. Otherwise, parse it. Find the issuer's key by `issuer` and `key_id` among keys the receiving app pinned, or from the issuer's key file. No key: it is not a Safe Action.
3. Verify:
   - the signature
   - that `app` is this app
   - that `action` is registered by this app, for this issuer
   - that the lifetime is no longer than the app allows (default 10 minutes, at most 1 hour)
   - that now is within it, with 2 minutes of clock tolerance
   - that this reader has not seen the `request_id` before
4. **Any check fails:** do nothing. Say that the code claims to be a Safe Action but could not be verified, with the reason. Do not offer to run it.
5. **Same-device**, by both the envelope's scope and the app's registration: run the registered handler at once, with no prompt.
6. **Cross-device**, by either the envelope's scope or the app's registration: the **SDK** shows a confirmation naming the verified issuer, the action, and the request information. The handler runs only after the user approves. The app cannot skip, replace or pre-approve this confirmation in an official SDK.
7. The `request_id` is single use. The reader remembers it locally; the issuer's server enforces it as the authority.

## What an issuer must do

- Keep the private key on the server, or in a key service that never releases it (ES256). Never put it in a page, an app, or a code.
- **Signing with a key service (ES256).** Build the signing input (`ckey_safe_action_signing_input`, or `ckey action signing-input`), ask the service for an ECDSA P-256 SHA-256 signature over it (or over its SHA-256 digest, if the service takes digests), convert the returned signature to low-S `r ‖ s` (`ckey_es256_signature_from_der` takes the DER most services return), then build the payload (`ckey_safe_action_assemble`, or `ckey action assemble`) and verify it once with the public key before publishing it.
- Give every Safe Action a fresh `request_id`, and accept each one once.
- Keep lifetimes short: a minute or two for sign-in, at most an hour for anything.
- For cross-device actions, serve `request_info` and accept an approval only from the receiving app's backend, for a pending request, once.
- Mark an action cross-device whenever its effect happens anywhere other than the scanning device.

## Security notes

- **Forgery:** without the private key nobody can make a valid envelope, or change an issuer, app, action, scope or parameter in one.
- **Replay:** single-use request IDs and short lifetimes stop old codes and screenshots from working later.
- **Relay:** someone can show a genuine cross-device code (for example a real sign-in code for *their own* browser session) somewhere you will scan it. A signature cannot detect that, because nothing in the code is fake. The cross-device confirmation, with the requesting device and place, is what lets a person notice. This is why cross-device actions are never fully automatic in the official SDKs.
- **Same-device actions** cannot be relayed into someone else's session, because their result stays on the scanning device.

## Unofficial auto-execution

`ckey-unsafe-autoexec` is a separate package that parses and verifies Safe Actions, but leaves when and how to run them, and any prompt, to the developer. CKey does nothing unsafe in it: the developer takes on the responsibility the official SDK otherwise keeps. Most developers who use it want to own the prompt and still follow the rules above. Products built on it may say they read CKey codes, but may not claim CKey Safe Actions, certification, or that an action was brokered by CKey ([trademark policy](https://openckey.org/trademark)).

## Conformance

`tests/vectors/safe-actions-v1.json` (Ed25519) and `tests/vectors/safe-actions-es256-v1.json` (ES256, including a high-S signature, a compressed key and a point off the curve) hold envelopes made by an implementation independent of the C core (`tools/safe_actions_vectors.mjs`, with Node's own Ed25519 and P-256), each with the verdict a conforming verifier must reach: `ok`, `integrity` (bad signature), `expired` (out of time or too long-lived), or `invalid` (malformed). The core's tests check every one. A reader may describe itself as supporting CKey Safe Actions only if it reaches every verdict, and follows [what a reader must do](#what-a-reader-must-do).

## Tools

```sh
ckey action keygen --out issuer.key
ckey action public-key --key issuer.key
ckey action sign --key issuer.key --issuer citeory.com --app com.viadere --action signin.request \
     --key-id 2026-11 --request sess-41fd0a9be2c74 --scope cross-device --ttl 120
ckey action verify --payload 'ckey-action:1:…' --public-key <hex>   # Ed25519 (64 hex) or ES256 (66/130 hex)

# ES256 with a key service that never releases the key:
ckey action signing-input --issuer viadere.com --app com.viadere --action signin.request \
     --key-id vault-p256-1 --request sess-0123456789ab --scope cross-device --ttl 120 --now 1794000000 > input.hex
#   ... the key service signs the bytes in input.hex (ECDSA P-256, SHA-256) and returns DER ...
ckey action assemble <the same fields and --now> --der-signature <hex> --public-key <66 hex>
```

The C ABI: `ckey_is_safe_action`, `ckey_safe_action_parse`, `ckey_safe_action_verify` (Ed25519), `ckey_safe_action_verify_key` (Ed25519 or ES256), `ckey_safe_action_sign` (Ed25519 from a seed), `ckey_safe_action_signing_input` and `ckey_safe_action_assemble` (signing elsewhere), `ckey_es256_signature_from_der`, `ckey_ed25519_public_key` ([`ckey.h`](../include/ckey/ckey.h); ES256 from 0.1.0-build.118, `CKEY_HAS_ES256`).
