// Request routing, logging and lifecycle for the openckey.org service.

import { createServer } from 'node:http';
import { makeAdminAuth } from './auth.js';
import { openDb } from './db.js';
import { dayNumberOfMs, utcDay } from './days.js';
import { HttpError, applySecurityHeaders, sendBody, sendError, sendJSON } from './http.js';
import { makeLinkHandlers } from './links.js';
import { PAGE_CSP, errorPage } from './pages.js';
import { RateLimiter } from './rate-limit.js';
import { makeStaticHandlers } from './static.js';
import { TelemetryStore, makeTelemetryHandlers } from './telemetry.js';
import { makeTextHandlers } from './texts.js';

const KNOWN_METHODS = new Set(['GET', 'HEAD', 'POST', 'PUT', 'PATCH', 'DELETE', 'OPTIONS']);

/**
 * Build the service. Options:
 * - config: from loadConfig()
 * - now: clock in ms (tests inject one)
 * - logger: (line) => void; defaults to console.log when config.log is true
 */
export function createApp({ config, now = Date.now, logger } = {}) {
  const log = logger ?? (config.log ? (line) => console.log(line) : () => {});
  const db = openDb(config.dbPath);
  const requireAdmin = makeAdminAuth(config.adminTokens);
  const limiter = new RateLimiter({
    ratePerHour: config.telemetryRatePerHour,
    burst: config.telemetryBurst,
    maxKeys: config.rateLimitMaxKeys,
    now,
  });
  const store = new TelemetryStore(db);
  const telemetry = makeTelemetryHandlers({ store, limiter, config, now });
  const links = makeLinkHandlers({ db, config, requireAdmin, now });
  const texts = makeTextHandlers({ db, config, requireAdmin, now });
  const statics = makeStaticHandlers({ config });

  // `html: true` routes answer errors with an HTML page for browsers.
  const routes = [
    { pattern: '/', re: /^\/$/, methods: { GET: statics.index } },
    { pattern: '/assets/:file', re: /^\/assets\/(?<file>[^/]+)$/, methods: { GET: statics.asset } },
    { pattern: '/sdk/:file', re: /^\/sdk\/(?<file>[^/]+)$/, methods: { GET: statics.sdk } },
    { pattern: '/robots.txt', re: /^\/robots\.txt$/, methods: { GET: statics.robots } },
    { pattern: '/healthz', re: /^\/healthz$/, methods: { GET: (req, res) => sendJSON(res, 200, { ok: true }) } },
    {
      pattern: '/api/telemetry/v1/batch',
      re: /^\/api\/telemetry\/v1\/batch$/,
      methods: { POST: telemetry.postBatch, OPTIONS: telemetry.preflight },
    },
    {
      pattern: '/api/telemetry/v1/summary',
      re: /^\/api\/telemetry\/v1\/summary$/,
      methods: { GET: telemetry.getSummary },
    },
    { pattern: '/api/links', re: /^\/api\/links$/, methods: { POST: links.create } },
    { pattern: '/api/links/:id', re: /^\/api\/links\/(?<id>[0-9A-Za-z]{7})$/, methods: { DELETE: links.remove } },
    {
      pattern: '/api/links/:id/stats',
      re: /^\/api\/links\/(?<id>[0-9A-Za-z]{7})\/stats$/,
      methods: { GET: links.getStats },
    },
    { pattern: '/r/:id', re: /^\/r\/(?<id>[0-9A-Za-z]{7})$/, methods: { GET: links.visit }, html: true },
    { pattern: '/api/texts', re: /^\/api\/texts$/, methods: { POST: texts.create } },
    { pattern: '/api/texts/:id', re: /^\/api\/texts\/(?<id>[0-9A-Za-z]{7})$/, methods: { DELETE: texts.remove } },
    {
      pattern: '/t/:id',
      re: /^\/t\/(?<id>[0-9A-Za-z]{7})(?<ext>\.txt)?$/,
      methods: { GET: texts.show },
      html: true,
    },
  ];

  function wantsHtml(req) {
    return /\btext\/html\b/.test(String(req.headers.accept || ''));
  }

  async function handle(req, res) {
    const start = process.hrtime.bigint();
    const method = KNOWN_METHODS.has(req.method) ? req.method : 'OTHER';
    let pattern = '(unmatched)';
    let html = false;
    res.on('finish', () => {
      const ms = Number(process.hrtime.bigint() - start) / 1e6;
      // Method, route pattern, status and duration only: never IPs, bodies or URLs.
      log(`${method} ${pattern} ${res.statusCode} ${ms.toFixed(1)}ms`);
    });
    applySecurityHeaders(res);
    try {
      let url;
      try {
        url = new URL(req.url, 'http://localhost');
      } catch {
        throw new HttpError(400, 'bad request');
      }
      let route;
      let m;
      for (const r of routes) {
        m = r.re.exec(url.pathname);
        if (m) {
          route = r;
          break;
        }
      }
      if (!route) throw new HttpError(404, 'not found');
      pattern = route.pattern;
      html = Boolean(route.html);
      const lookup = method === 'HEAD' ? 'GET' : method;
      const fn = route.methods[lookup];
      if (!fn) {
        const allow = Object.keys(route.methods);
        if (allow.includes('GET')) allow.push('HEAD');
        throw new HttpError(405, 'method not allowed', { headers: { Allow: allow.join(', ') } });
      }
      await fn(req, res, { params: { ...m.groups }, url, method });
    } catch (err) {
      let e = err;
      if (!(err instanceof HttpError)) {
        // Log the stack frames only; the message line could echo request data.
        const frames = String(err?.stack || '').split('\n').slice(1, 6).join('\n');
        log(`error in ${method} ${pattern}: ${err?.name || 'Error'}${err?.code ? ` (${err.code})` : ''}\n${frames}`);
        e = new HttpError(500, 'internal error');
      }
      if (html && !res.headersSent && wantsHtml(req)) {
        const headers = { ...e.headers, 'Cache-Control': 'no-store', 'Content-Security-Policy': PAGE_CSP };
        if (e.close) headers.Connection = 'close';
        sendBody(res, e.status, 'text/html; charset=utf-8', errorPage(e.status, e.message), headers);
      } else {
        sendError(res, e);
      }
    }
  }

  // Maintenance: salt rotation (lazy, also checked here), retention, pruning.
  let lastMaintenanceDay = null;
  function runMaintenance() {
    limiter.maybeRotate();
    limiter.prune();
    const today = utcDay(now());
    if (today !== lastMaintenanceDay) {
      lastMaintenanceDay = today;
      const removed = store.purge(dayNumberOfMs(now()));
      if (removed) log(`retention: removed ${removed} aggregate rows older than 400 days`);
    }
  }
  runMaintenance();
  const timer = setInterval(runMaintenance, 60_000);
  timer.unref();

  const server = createServer({ requestTimeout: 30_000, headersTimeout: 15_000, keepAliveTimeout: 5_000 }, handle);
  server.maxHeadersCount = 64;

  let closing = null;
  function close({ graceMs = 10_000 } = {}) {
    if (closing) return closing;
    clearInterval(timer);
    closing = new Promise((resolve) => {
      const force = setTimeout(() => server.closeAllConnections(), graceMs);
      force.unref();
      server.close(() => {
        clearTimeout(force);
        try {
          db.close();
        } catch {
          /* already closed */
        }
        resolve();
      });
      server.closeIdleConnections();
    });
    return closing;
  }

  return { server, db, store, limiter, handle, runMaintenance, close };
}
