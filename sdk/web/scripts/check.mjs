// `node --check` every JavaScript file of the SDK (and, with --dist, require
// that dist/ is assembled, so `npm pack` never ships an unbuilt package).
import { execFileSync } from 'node:child_process';
import { existsSync, readdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const files = [];
for (const dir of ['js', 'scripts', 'test', 'dist']) {
  const d = join(root, dir);
  if (!existsSync(d)) continue;
  for (const f of readdirSync(d)) if (/\.(mjs|js)$/.test(f)) files.push(join(d, f));
}
for (const f of files) execFileSync(process.execPath, ['--check', f], { stdio: 'inherit' });
console.log(`node --check: ${files.length} files OK`);
if (process.argv.includes('--dist')) {
  for (const f of ['ckey.mjs', 'ckey-core.mjs', 'ckey.wasm', 'ckey.d.ts']) {
    if (!existsSync(join(root, 'dist', f))) {
      console.error(`dist/${f} is missing: run npm run build first`);
      process.exit(1);
    }
  }
}
