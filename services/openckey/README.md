# openckey.org service

This is one Node service with no dependencies. It runs the parts of openckey.org that need a server:

- **Telemetry ingestion** for CKey SDKs. It implements [`docs/adoption-and-telemetry.md`](../../docs/adoption-and-telemetry.md), which is normative, and specification §9.
- **Short links** (`/r/:id`), so a small CKey can carry a long URL. Each link opens an interstitial page that shows the destination host first.
- **Hosted text** (`/t/:id`), a plain, escaped reading page for longer text.
- **The web renderer page** (`/`), where people make a CKey in the browser with the CKey web SDK.

It uses only Node 22 built-ins: `node:http`, `node:sqlite`, `node:crypto`, `node:fs` and `node:test`. Nothing needs installing.

```sh
npm start        # node --no-warnings server.js
npm test         # node --test --no-warnings
```

Node 22.5 or newer is required, because `node:sqlite` first ships in 22.5. Node still marks `node:sqlite` as experimental, and `server.js` hides that one warning.

## Routes

| Method | Path | Auth | Purpose |
|---|---|---|---|
| `GET` | `/` | – | The CKey renderer page (`public/index.html`) |
| `GET` | `/assets/:file` | – | The page's scripts, styles and icon (`public/assets/`) |
| `GET` | `/sdk/:file` | – | Web SDK files (`ckey.mjs`, `ckey.wasm`) from `OPENCKEY_SDK_DIR`. Returns 404 if the SDK is not built |
| `GET` | `/robots.txt`, `/healthz` | – | Crawler rules and a liveness check |
| `POST` | `/api/telemetry/v1/batch` | – | Ingest one schema-1 batch. Replies `204`, `400`, `413`, `415` or `429` |
| `OPTIONS` | `/api/telemetry/v1/batch` | – | CORS preflight, for the web SDK on third-party origins |
| `GET` | `/api/telemetry/v1/summary?days=30` | – | Labelled public totals. `days` is 1–400 and defaults to 30 |
| `POST` | `/api/links` | admin | `{"url": "...", "count": false}` → `201 {"id", "short_url"}` |
| `GET` | `/r/:id` | – | Interstitial page with the host first, the full URL, and a "Continue to host" link |
| `GET` | `/r/:id?go=1` | – | `302` to the destination, for integrators that have already shown the host |
| `GET` | `/api/links/:id/stats` | admin | `{"id", "url", "count", "created_at", "disabled", "total", "days": [{"day", "hits"}]}` |
| `DELETE` | `/api/links/:id` | admin | Disable the link (`204`). After that, `/r/:id` returns `410` |
| `POST` | `/api/texts` | admin | `{"text": "...", "title": "optional"}` → `201 {"id", "url", "text_url"}` |
| `GET` | `/t/:id` | – | Text page: escaped, newlines kept, links not turned into hyperlinks |
| `GET` | `/t/:id.txt` | – | The same text as `text/plain; charset=utf-8` |
| `DELETE` | `/api/texts/:id` | admin | Delete the text (`204`). Its content is purged, and later requests get `410` |

"admin" means the request carries `Authorization: Bearer <token>`, with a token from `OPENCKEY_ADMIN_TOKENS`. Each token is compared as a SHA-256 digest with `crypto.timingSafeEqual`, and every token is checked on each request. When no tokens are configured, admin routes refuse every request.

Errors come back as `{"error": "..."}`. The exception is a browser asking for an HTML page (`Accept: text/html`) on `/r/:id` or `/t/:id`: it gets a small HTML error page with the same status. `GET` routes also answer `HEAD`. A known path called with the wrong method gets `405` and an `Allow` header.

### Telemetry validation

The batch endpoint applies the field rules in `docs/adoption-and-telemetry.md` exactly:

- **Keys:** every object must have exactly the listed keys. Unknown or missing keys, at any depth, get `400`.
- **Enums and version:** `sdk` and `platform` are enums, and `sdk_version` must match the version regex.
- **Numbers:** every count is an integer from 0 to 10,000,000, and every `sum` is an integer from 0 to 10¹².
- **Histograms:** `buckets` holds exactly 10 integers, and they must add up to `count`.
- **Day:** `day` is a real `YYYY-MM-DD` date, from 35 days before to 2 days after the server's current UTC day.

The checks run in this order:

1. **Declared size:** a `Content-Length` over 4,096 bytes gets `413` before any of the body is read.
2. **Content type:** a `Content-Type` other than `application/json` gets `415`. The only parameter allowed is `charset=utf-8`.
3. **Per-key rate limit:** over the limit gets `429` with `Retry-After`.
4. **Body read:** the body is read up to 4,096 bytes, and a chunked body that runs past that gets `413`. In both `413` cases the connection is closed, so the rest of the body is never read.
5. **Parsing:** the body must be strict UTF-8 JSON, then pass the schema check, or it gets `400`. The parser is stricter than the schema text in three ways. It rejects duplicate keys and number literals with a fraction or exponent, so `1.0` is not accepted as an integer. It also rejects a BOM.
6. **Daily cap:** once the global daily cap is reached, the batch gets `429`.
7. **Accepted:** the batch is summed into `telemetry_daily` (day, sdk, sdk_version, platform), and the reply is `204`.

The summary always includes `"label": "Reported by SDKs with telemetry on; not total usage, unique users, or worldwide scans."` and a short methodology note. It is served with `Access-Control-Allow-Origin: *`.

### Short links

- **Allowed destinations:** only `http:` and `https:` URLs with a host, at most 2,048 characters. A URL is rejected if it has user credentials (`user:pass@`, a common way to disguise the real host), whitespace or control characters. The stored URL is the WHATWG-normalized `href`.
- **Interstitial page:** it shows the host first (`URL.host`, with internationalized names in punycode form), then the full URL. It contains no script and no automatic refresh, and it warns when the destination uses plain `http:`. This follows safety rule 1 in `spec/CKey-Payload-Conventions.md`: never act automatically, and show the host first.
- **Ids:** 7 characters of base62 from `crypto.randomBytes` with rejection sampling, which gives 62⁷ ≈ 3.5 × 10¹² possible ids.
- **Counting:** when a link was created with `"count": true`, each `GET` on `/r/:id` (the page or `?go=1`) adds one to a per-link, per-day counter. Nothing else is recorded: no IP, user agent, referrer or time of day. `HEAD` requests, such as link previews, are not counted. A link without `count` records nothing.

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `8080` | Listen port |
| `OPENCKEY_HOST` | all interfaces | Listen address. Use `127.0.0.1` when a local proxy sits in front |
| `OPENCKEY_DB` | `./data/openckey.sqlite` | SQLite file. A relative path resolves against the working directory, and missing directories are created |
| `OPENCKEY_PUBLIC_BASE` | `https://openckey.org` | Base URL used in `short_url` and `url` responses |
| `OPENCKEY_ADMIN_TOKENS` | empty | Comma-separated bearer tokens for admin routes. Use long random values, for example `openssl rand -base64 32` |
| `OPENCKEY_TRUST_PROXY` | unset | `1` uses the first `X-Forwarded-For` hop as the client IP for rate limiting |
| `OPENCKEY_SDK_DIR` | `../../sdk/web/dist` (relative to this directory) | Where `/sdk/*` files come from |
| `OPENCKEY_TELEMETRY_RATE_PER_HOUR` | `30` | Token refill rate per rate-limit key |
| `OPENCKEY_TELEMETRY_BURST` | `30` | Bucket size per key |
| `OPENCKEY_TELEMETRY_DAILY_CAP` | `200000` | Accepted batches per server UTC day, across all senders |
| `OPENCKEY_RATE_LIMIT_MAX_KEYS` | `200000` | Memory bound on live rate-limit buckets |
| `OPENCKEY_LOG` | on | `0` silences request logs |

If a numeric variable holds an invalid value, the service refuses to start.

## Deployment

The service runs on any Node 22.5+ host behind a TLS-terminating reverse proxy or CDN, such as Caddy, nginx or a managed load balancer:

1. **Copy the files.** Put this directory on the host, along with the built web SDK, or point `OPENCKEY_SDK_DIR` at it.
2. **Set the environment.** Set `OPENCKEY_ADMIN_TOKENS` and `OPENCKEY_DB`, on a persistent volume, and run `npm start` under a supervisor such as systemd or a container runtime.
3. **Expose it only through the proxy.** Bind to `127.0.0.1` with `OPENCKEY_HOST`, or keep the port private. Set `OPENCKEY_TRUST_PROXY=1` only if the proxy **overwrites** `X-Forwarded-For` with the real client address. If it appends to a header the client sent, the first hop comes from the client, and anyone can forge it to get around the per-key limit.
4. **Shut down gracefully.** `SIGTERM` or `SIGINT` stops accepting new connections and lets in-flight requests finish. After 10 seconds the remaining connections are closed, and then the database is closed.
5. **Back up the database.** It is a single SQLite file in WAL mode. Use `sqlite3 openckey.sqlite ".backup out.sqlite"` while the service runs.

Every response carries `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`, `X-Frame-Options: DENY`, COOP, CORP, `Permissions-Policy`, HSTS and a restrictive CSP:

- **Renderer page:** `script-src 'self' 'wasm-unsafe-eval'`.
- **Text and interstitial pages:** `default-src 'none'; style-src 'unsafe-inline'`.

## Privacy obligations

These rules come from `docs/adoption-and-telemetry.md` ("Service obligations") and specification §9.2. Any change to the service must keep them.

- **Storage:**
  - Telemetry is stored only as sums per (day, sdk, sdk_version, platform). Raw request bodies are never stored.
  - The only other telemetry table, `telemetry_ingest`, counts accepted batches per receive day, for the daily cap.
- **IP addresses:**
  - IPs never go into the database or the application logs.
  - Request logs contain only the method, route pattern (for example `/r/:id`, never the real path), status and duration.
  - Error logs include stack frames, not error messages, because messages can echo request data.
- **Rate limiting:**
  - The limiter key is `HMAC-SHA256(salt, client IP)`. The salt is 32 random bytes held only in memory, and a new one is generated at every UTC day boundary.
  - All buckets are dropped at each rotation, so keys from different days cannot be linked.
  - Nothing derived from an IP is persisted, and a restart also replaces the salt.
- **Retention:** telemetry aggregates and the daily ingest counters older than 400 days are deleted at startup and on each new UTC day.
- **Reporting:**
  - The public summary is always labelled as figures *reported by SDKs with telemetry on*, which are not total usage, unique users or worldwide scans.
  - The figures are unauthenticated and can be spoofed or inflated. Publish the methodology with them.
  - Link counts are separate adoption evidence and must never be merged into one "users" number.
- **Short links:** counting is opt-in for each link, and only a per-day total is kept.
- **Hosted text:** deleting a text purges its content from the database.

### Your proxy and CDN can still see IPs

This service never keeps client IPs, but the network in front of it does see them. The service having its own address does not hide the client's source IP (spec §9.2). The operator of openckey.org must therefore:

- **Proxy and CDN logs:** turn off access logs on the TLS proxy or CDN for `/api/telemetry/*` and `/r/*`, or truncate or anonymize the IPs in them and keep them only briefly. A default nginx, Caddy or CDN setup logs full IPs and full request paths.
- **Abuse protection:** check what IP data a DDoS or bot-protection layer keeps, and for how long.
- **Privacy notice:** list in the openckey.org privacy notice who processes this data and how long it is kept. IP processing can still fall under privacy law even when the payload holds only counters.
- **No analytics:** do not add analytics, tracking pixels or third-party scripts to any page this service serves.

## Web renderer

`public/index.html` and `public/assets/app.js` load the web SDK from `/sdk/ckey.mjs`, which in turn loads `ckey.wasm` from the same place. The page then draws the symbol on a `<canvas>`. People can choose:

- the Profile 0 version (auto, or 1–31);
- the error correction level (L, M, Q or H);
- the layout (standard or print);
- the size in pixels;
- a transparent background.

The page offers PNG and SVG downloads. It also shows a hint naming the detected payload convention: https/http, mailto, tel, sms, geo, address, vCard, or plain text. Detection follows `spec/CKey-Payload-Conventions.md` and lives in `public/assets/conventions.js`, which the tests cover. If the SDK is missing, the page says the renderer has not been built yet and leaves the rest of the page usable. Nothing typed on the page leaves the browser.

## Layout

```
server.js            entry point: config, listen, graceful shutdown
src/app.js           router, request logging, maintenance timer
src/config.js        environment parsing
src/db.js            sqlite schema (node:sqlite)
src/telemetry.js     schema-1 validation, aggregation, summary, retention
src/rate-limit.js    salted-HMAC token buckets, daily salt rotation
src/strict-json.js   strict JSON parser (duplicate keys, integer literals)
src/links.js         short links and interstitial
src/texts.js         hosted text
src/pages.js         server-rendered HTML (escaped)
src/static.js        public/ and SDK files
src/auth.js          bearer token check
src/http.js          headers, bounded body reading, helpers
public/              renderer page
test/                node:test suites
```
