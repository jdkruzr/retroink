import { cp, mkdir, readdir, stat } from 'node:fs/promises';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

// fileURLToPath (not `.pathname`) is required here: `.pathname` returns the
// URL-encoded path component, so any repo checked out under a directory with
// a space or other reserved character (e.g. "New project/retroink") silently
// resolves to a nonexistent %20-escaped path and every copy below becomes a
// no-op with no error.
const siteRoot = resolve(fileURLToPath(new URL('..', import.meta.url)));
const repoRoot = resolve(siteRoot, '..');

async function copyIfPresent(from, to) {
  try {
    await stat(from);
  } catch {
    return;
  }
  await mkdir(dirname(to), { recursive: true });
  await cp(from, to, { recursive: true, force: true });
}

async function copyDirectoryWithoutMacMetadata(from, to) {
  try {
    const sourceStat = await stat(from);
    if (!sourceStat.isDirectory()) {
      return;
    }
  } catch {
    return;
  }

  await mkdir(to, { recursive: true });
  const entries = await readdir(from, { withFileTypes: true });
  for (const entry of entries) {
    if (entry.name === '.DS_Store') {
      continue;
    }
    const source = join(from, entry.name);
    const target = join(to, entry.name);
    if (entry.isDirectory()) {
      await copyDirectoryWithoutMacMetadata(source, target);
    } else if (entry.isFile()) {
      await mkdir(dirname(target), { recursive: true });
      await cp(source, target, { force: true });
    }
  }
}

await copyDirectoryWithoutMacMetadata(resolve(repoRoot, 'docs/images'), resolve(siteRoot, 'public/images'));
await copyIfPresent(resolve(repoRoot, 'web/assets/logo.png'), resolve(siteRoot, 'public/logo.png'));
await copyIfPresent(resolve(repoRoot, 'docs/catalog'), resolve(siteRoot, 'public/catalog'));
await copyIfPresent(resolve(repoRoot, 'docs/CNAME'), resolve(siteRoot, 'public/CNAME'));
