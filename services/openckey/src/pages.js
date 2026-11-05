// Server-rendered HTML pages: link interstitial, hosted text, error pages.
// Every interpolated value goes through escapeHtml.

import { escapeHtml as e } from './http.js';

/** CSP for server-rendered pages: no scripts, inline styles only. */
export const PAGE_CSP =
  "default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'; frame-ancestors 'none'";

const STYLE = `
:root { color-scheme: light dark; --bg:#fbfbfa; --fg:#1b1d1f; --muted:#5d6268; --card:#ffffff; --line:#dfe1e4;
  --accent:#0b57d0; --accent-fg:#ffffff; --warn:#8a4b00; }
@media (prefers-color-scheme: dark) { :root { --bg:#141517; --fg:#e8e9eb; --muted:#a3a8ae; --card:#1d1f22; --line:#33363a;
  --accent:#8ab4f8; --accent-fg:#0b1f3a; --warn:#f2b24c; } }
* { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body { margin:0; background:var(--bg); color:var(--fg); font:16px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif; }
main { max-width: 44rem; margin: 0 auto; padding: 2.5rem 16px 3rem; }
.card { background:var(--card); border:1px solid var(--line); border-radius:12px; padding:1.5rem; }
.eyebrow { margin:0 0 .25rem; color:var(--muted); font-size:.9rem; }
.host { margin:0 0 1rem; font-size:clamp(1.6rem, 6vw, 2.4rem); line-height:1.2; overflow-wrap:anywhere; }
.label { margin:1rem 0 .25rem; color:var(--muted); font-size:.85rem; }
.url { margin:0; font:14px/1.5 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; overflow-wrap:anywhere; word-break:break-all; }
.btn { display:inline-block; margin-top:1.5rem; padding:.7rem 1.2rem; border-radius:8px; background:var(--accent);
  color:var(--accent-fg); text-decoration:none; font-weight:600; overflow-wrap:anywhere; }
.btn:focus-visible { outline:3px solid var(--fg); outline-offset:2px; }
.warn { color:var(--warn); font-weight:600; }
.note { margin-top:1.5rem; color:var(--muted); font-size:.9rem; }
h1.title { margin:0 0 1rem; font-size:1.6rem; line-height:1.25; overflow-wrap:anywhere; }
pre.text { margin:0; white-space:pre-wrap; overflow-wrap:anywhere; font:inherit; font-size:1.05rem; line-height:1.65; tab-size:4; }
footer { margin-top:1.25rem; color:var(--muted); font-size:.85rem; }
footer a { color:inherit; }
`;

function layout(title, body) {
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<meta name="robots" content="noindex, nofollow">
<title>${e(title)}</title>
<style>${STYLE}</style>
</head>
<body>
<main>
${body}
</main>
</body>
</html>
`;
}

/**
 * Interstitial for a short link. The destination host is the most prominent
 * element; the full URL is shown below it. Nothing happens until the user
 * follows the "Continue" link (CKey Payload Conventions, safety rule 1).
 */
export function interstitialPage(destination) {
  const u = new URL(destination);
  const host = u.host;
  const insecure = u.protocol === 'http:';
  return layout(
    `Continue to ${host}?`,
    `<div class="card">
<p class="eyebrow">This CKey short link goes to</p>
<h1 class="host">${e(host)}</h1>
${insecure ? '<p class="warn">This address does not use HTTPS. Data you send may be readable by others.</p>' : ''}
<p class="label">Full address</p>
<p class="url">${e(u.href)}</p>
<a class="btn" href="${e(u.href)}" rel="noopener noreferrer nofollow" referrerpolicy="no-referrer">Continue to ${e(host)}</a>
<p class="note">openckey.org does not check where short links lead. Continue only if you trust ${e(host)}.</p>
</div>`,
  );
}

/** Hosted text. The text is escaped and shown as-is; links are not activated. */
export function textPage(id, title, body) {
  const pageTitle = title || 'Text';
  return layout(
    pageTitle,
    `<article class="card">
${title ? `<h1 class="title">${e(title)}</h1>` : ''}
<pre class="text">${e(body)}</pre>
</article>
<footer>Hosted on openckey.org &middot; <a href="/t/${e(id)}.txt">plain text</a></footer>`,
  );
}

export function errorPage(status, message) {
  return layout(
    message,
    `<div class="card"><p class="eyebrow">Error ${Number(status)}</p><h1 class="title">${e(message)}</h1></div>`,
  );
}
