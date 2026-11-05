// Assembles dist/ from an Emscripten build directory:
//   ckey-core.mjs  Emscripten glue (patched to load ckey.wasm)
//   ckey.wasm      the core
//   ckey.mjs, api.mjs, options.mjs, payload.mjs, scanner.mjs, telemetry.mjs
//   ckey.d.ts      copied from types/ckey.d.ts
// Usage: node scripts/assemble.mjs <emscripten build dir>
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const build = resolve(process.argv[2] ?? join(root, 'build/wasm'));
const dist = join(root, 'dist');
mkdirSync(dist, { recursive: true });

const glue = join(build, 'ckey-core.mjs');
const wasm = join(build, 'ckey-core.wasm');
for (const f of [glue, wasm]) {
  if (!existsSync(f)) {
    console.error(`missing ${f}; run the Emscripten build first (scripts/build.sh)`);
    process.exit(1);
  }
}
// The glue names its binary after the build target; the package ships it as
// ckey.wasm (the name the openckey.org page and loadCKey expect).
const src = readFileSync(glue, 'utf8');
const patched = src.replaceAll('ckey-core.wasm', 'ckey.wasm');
if (patched === src) console.warn('warning: ckey-core.wasm not referenced by the glue; loadCKey passes locateFile anyway');
writeFileSync(join(dist, 'ckey-core.mjs'), patched);
copyFileSync(wasm, join(dist, 'ckey.wasm'));

for (const f of readdirSync(join(root, 'js'))) {
  if (f.endsWith('.mjs')) copyFileSync(join(root, 'js', f), join(dist, f));
}
copyFileSync(join(root, 'types/ckey.d.ts'), join(dist, 'ckey.d.ts'));
console.log(`assembled ${dist}`);
