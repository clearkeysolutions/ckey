#!/usr/bin/env node
// openckey.org service entry point.

// node:sqlite is still flagged experimental in Node 22; hide only that warning.
const emitWarning = process.emitWarning;
process.emitWarning = function (warning, ...rest) {
  const msg = typeof warning === 'string' ? warning : warning?.message;
  if (typeof msg === 'string' && msg.includes('SQLite is an experimental feature')) return;
  return emitWarning.call(process, warning, ...rest);
};

const { loadConfig } = await import('./src/config.js');
const { createApp } = await import('./src/app.js');

const config = loadConfig();
if (config.adminTokens.length === 0) {
  console.log('note: OPENCKEY_ADMIN_TOKENS is empty; admin routes will refuse every request');
}
const app = createApp({ config });

app.server.listen(config.port, config.host, () => {
  const addr = app.server.address();
  console.log(`openckey service listening on port ${addr.port}`);
});

let stopping = false;
async function shutdown(signal) {
  if (stopping) return;
  stopping = true;
  console.log(`${signal}: shutting down`);
  await app.close();
  process.exit(0);
}
process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));
