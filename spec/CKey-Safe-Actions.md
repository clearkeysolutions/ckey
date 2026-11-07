# CKey Safe Actions (draft 1)

A **Safe Action** is a code that a CKey reader may act on **without asking first**. It is the one exception to the rule that a reader shows what a code holds and acts only on the user's approval ([payload conventions](CKey-Payload-Conventions.md), rule 1).

The exception is narrow by design. A Safe Action is a signed request from a known site (the **issuer**) to a known app (the **receiving app**) to run one action that app has registered. The receiving app's SDK verifies it before anything happens, and anything that does not verify is ordinary text.

Example: on `citeory.com`, a "Sign in with Viadere" code carries a Safe Action for the app `com.viadere`, action `signin.request`, signed by Citeory's key. Viadere's app verifies it and starts the sign-in.

## What CKey does and does not vouch for

The format, the verification rules and the official SDKs make sure an action is **genuine**: it came from the issuer it names, for the app and action it names, unchanged, once, and in time. CKey never vouches for **what** an action does. That is the issuer's and the receiving app's responsibility. openckey.org certifies how a code looks and scans, never its content ([trademark policy](https://openckey.org/trademark)).

## Terms

- **Issuer:** the site that creates and signs Safe Actions, named by its DNS name, for example `citeory.com`. It holds an Ed25519 private key on its own server and never shares it.
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
| signature | 64 bytes | Ed25519 (RFC 8032) |

The signature covers the ASCII text `CKey Safe Action v1`, one zero byte, then every envelope byte before the signature. Verifiers reject a signature whose scalar S is not below the group order (RFC 8032 section 5.1.7). Nothing may follow the signature.

## Issuer keys

An issuer publishes its public keys at:

```
https://<issuer>/.well-known/ckey-safe-actions.json
```

```json
{
  "issuer": "citeory.com",
  "keys": [
    { "id": "2026-11", "alg": "Ed25519", "public_key": "<64 hex digits>", "not_after": "2027-11-01T00:00:00Z" }
  ],
  "request_info": "https://citeory.com/api/ckey/requests/{request_id}"
}
```

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

- Keep the private key on the server. Never put it in a page, an app, or a code.
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

`tests/vectors/safe-actions-v1.json` holds envelopes made by an implementation independent of the C core (`tools/safe_actions_vectors.mjs`), each with the verdict a conforming verifier must reach: `ok`, `integrity` (bad signature), `expired` (out of time or too long-lived), or `invalid` (malformed). The core's tests check every one. A reader may describe itself as supporting CKey Safe Actions only if it reaches every verdict, and follows [what a reader must do](#what-a-reader-must-do).

## Tools

```sh
ckey action keygen --out issuer.key
ckey action public-key --key issuer.key
ckey action sign --key issuer.key --issuer citeory.com --app com.viadere --action signin.request \
     --key-id 2026-11 --request sess-41fd0a9be2c74 --scope cross-device --ttl 120
ckey action verify --payload 'ckey-action:1:…' --public-key <hex>
```

The C ABI: `ckey_is_safe_action`, `ckey_safe_action_parse`, `ckey_safe_action_verify`, `ckey_safe_action_sign`, `ckey_ed25519_public_key` ([`ckey.h`](../include/ckey/ckey.h)).
