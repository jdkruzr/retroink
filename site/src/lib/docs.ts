type DocModule = {
  frontmatter: {
    title?: string;
    nav_order?: number;
    parent?: string;
    has_children?: boolean;
  };
  Content: any;
};

export type DocEntry = {
  path: string;
  slug: string;
  url: string;
  title: string;
  navOrder: number;
  parent?: string;
  hasChildren: boolean;
  load: () => Promise<DocModule>;
};

const modules = import.meta.glob<DocModule>("../../../docs/**/*.md");

// RetroInk's docs nav is intentionally small: this is a personal fork, not a
// full docs portal, and most of the ~25 files under docs/ are CrossInk's own
// generic reference material (inherited as-is, not RetroInk-specific). Only
// these show up in the sidebar and the homepage's "Useful docs" list; the
// rest stay in the repo and stay routable (a direct link still works), they
// just aren't surfaced as if they were RetroInk's own documentation.
const FEATURED_SLUGS = ["whats-different", "installation", "obsidian-sync"];

const publicModules = Object.fromEntries(
  Object.entries(modules).filter(([path]) => !path.includes("/docs/development/")),
);

const visibleModules = Object.fromEntries(
  Object.entries(publicModules).filter(([path]) => FEATURED_SLUGS.includes(slugFromPath(path))),
);

function titleFromSlug(slug: string) {
  const leaf = slug.split("/").pop() ?? slug;
  return leaf.replace(/-/g, " ").replace(/\b\w/g, (char) => char.toUpperCase());
}

function slugFromPath(path: string) {
  let slug = path.replace("../../../docs/", "").replace(/\.md$/, "");
  if (slug.endsWith("/README")) {
    slug = slug.replace(/\/README$/, "/index");
  }
  if (slug === "index") {
    slug = "docs";
  }
  return slug;
}

// Astro's BASE_URL doesn't reliably carry a trailing slash across versions.
// Normalize once so every `${BASE}path` concatenation in this project is safe.
export const BASE = import.meta.env.BASE_URL.endsWith('/')
  ? import.meta.env.BASE_URL
  : `${import.meta.env.BASE_URL}/`;

function urlFromSlug(slug: string) {
  return `${BASE}${slug}.html`;
}

async function getEntries(sourceModules: typeof modules): Promise<DocEntry[]> {
  const entries = await Promise.all(
    Object.entries(sourceModules).map(async ([path, load]) => {
      const mod = await load();
      const slug = slugFromPath(path);
      return {
        path,
        slug,
        url: urlFromSlug(slug),
        title: mod.frontmatter.title ?? titleFromSlug(slug),
        navOrder: mod.frontmatter.nav_order ?? 999,
        parent: mod.frontmatter.parent,
        hasChildren: mod.frontmatter.has_children ?? false,
        load,
      };
    }),
  );

  return entries.sort((a, b) => {
    if (a.navOrder !== b.navOrder) {
      return a.navOrder - b.navOrder;
    }
    return a.title.localeCompare(b.title);
  });
}

export function getDocs(): Promise<DocEntry[]> {
  return getEntries(visibleModules);
}

export function getRoutableDocs(): Promise<DocEntry[]> {
  return getEntries(publicModules);
}

export async function getDocBySlug(slug: string) {
  const docs = await getRoutableDocs();
  return docs.find((doc) => doc.slug === slug);
}
