#!/usr/bin/env node
/**
 * SolWear OS documentation site generator.
 *
 * Deliberately dependency-free: `node docs/build.mjs` works on a clean clone
 * with nothing installed but Node itself. It renders the Markdown in
 * docs/pages plus docs/ARCHITECTURE.md into a static site in docs/dist.
 *
 * The Markdown subset supported is the one the documentation actually uses:
 * ATX headings, fenced code blocks, GitHub-flavoured tables, ordered and
 * unordered lists, blockquotes, horizontal rules, paragraphs, and the inline
 * constructs code / bold / italic / links / autolinks. If a page needs
 * something outside that subset, extend this file rather than adding a
 * dependency.
 */

import {
  readFileSync,
  writeFileSync,
  mkdirSync,
  rmSync,
  copyFileSync,
  existsSync,
} from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const DOCS = dirname(fileURLToPath(import.meta.url));
const OUT = join(DOCS, "dist");
const site = JSON.parse(readFileSync(join(DOCS, "site.json"), "utf8"));

/* ------------------------------------------------------------------ */
/* Inline rendering                                                    */
/* ------------------------------------------------------------------ */

const escapeHtml = (s) =>
  s
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");

/**
 * Source file name -> output page, derived from site.json. This is what lets a
 * page link to `../ARCHITECTURE.md` or `getting-started.md` and land on the
 * right built page regardless of where the source file lives.
 */
const linkMap = new Map(
  site.nav.flatMap((s) => s.items.map((it) => [it.src.split("/").pop(), it.out])),
);

/** Rewrite in-repo Markdown links so they resolve inside the built site. */
function rewriteHref(href) {
  if (/^(https?:|mailto:|#)/.test(href)) return href;
  const [path, hash] = href.split("#");
  const name = path.split("/").pop();
  const mapped = linkMap.get(name);
  const out = mapped || (path.endsWith(".md") ? name.slice(0, -3) + ".html" : path);
  return hash ? `${out}#${hash}` : out;
}

function inline(src) {
  // Protect code spans first, so their contents are never treated as markup.
  const spans = [];
  let text = src.replace(/`([^`]+)`/g, (_, code) => {
    spans.push(`<code>${escapeHtml(code)}</code>`);
    return `@@CODESPAN${spans.length - 1}@@`;
  });

  text = escapeHtml(text);
  text = text.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (_, label, href) => {
    const target = /^https?:/.test(href) ? ' target="_blank" rel="noreferrer"' : "";
    return `<a href="${rewriteHref(href)}"${target}>${label}</a>`;
  });
  text = text.replace(
    /(^|[\s(])&lt;(https?:\/\/[^\s>]+)&gt;/g,
    '$1<a href="$2" target="_blank" rel="noreferrer">$2</a>',
  );
  text = text.replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>");
  text = text.replace(/(^|[^*\w])\*([^*\n]+)\*(?!\*)/g, "$1<em>$2</em>");
  text = text.replace(/@@CODESPAN(\d+)@@/g, (_, i) => spans[Number(i)]);
  return text;
}

const slug = (s) =>
  s
    .toLowerCase()
    .replace(/`/g, "")
    .replace(/[^\w\s-]/g, "")
    .trim()
    .replace(/\s+/g, "-");

/* ------------------------------------------------------------------ */
/* Block rendering                                                     */
/* ------------------------------------------------------------------ */

function render(md) {
  const lines = md.replace(/\r\n/g, "\n").split("\n");
  const out = [];
  const headings = [];
  const para = [];
  let i = 0;

  const flushParagraph = () => {
    if (para.length) out.push(`<p>${inline(para.join(" "))}</p>`);
    para.length = 0;
  };

  while (i < lines.length) {
    const line = lines[i];

    // Fenced code block
    const fence = /^```(\w+)?\s*$/.exec(line);
    if (fence) {
      flushParagraph();
      const lang = fence[1] || "text";
      const body = [];
      i++;
      while (i < lines.length && !/^```\s*$/.test(lines[i])) body.push(lines[i++]);
      i++; // closing fence
      out.push(`<pre class="lang-${lang}"><code>${escapeHtml(body.join("\n"))}</code></pre>`);
      continue;
    }

    // Heading
    const head = /^(#{1,6})\s+(.*)$/.exec(line);
    if (head) {
      flushParagraph();
      const level = head[1].length;
      const title = head[2].trim();
      const id = slug(title);
      if (level === 2 || level === 3) headings.push({ level, title, id });
      out.push(
        `<h${level} id="${id}"><a class="anchor" href="#${id}">${inline(title)}</a></h${level}>`,
      );
      i++;
      continue;
    }

    // Horizontal rule
    if (/^(-{3,}|\*{3,}|_{3,})\s*$/.test(line)) {
      flushParagraph();
      out.push("<hr>");
      i++;
      continue;
    }

    // Table
    if (
      /^\s*\|/.test(line) &&
      i + 1 < lines.length &&
      /^\s*\|[\s:|-]+\|\s*$/.test(lines[i + 1])
    ) {
      flushParagraph();
      const cells = (row) =>
        row
          .trim()
          .replace(/^\|/, "")
          .replace(/\|$/, "")
          .split("|")
          .map((c) => c.trim());
      const header = cells(line);
      i += 2;
      const body = [];
      while (i < lines.length && /^\s*\|/.test(lines[i])) body.push(cells(lines[i++]));
      const thead = header.map((c) => `<th>${inline(c)}</th>`).join("");
      const tbody = body
        .map((r) => `<tr>${r.map((c) => `<td>${inline(c)}</td>`).join("")}</tr>`)
        .join("");
      out.push(
        `<div class="table-wrap"><table><thead><tr>${thead}</tr></thead><tbody>${tbody}</tbody></table></div>`,
      );
      continue;
    }

    // Blockquote
    if (/^>\s?/.test(line)) {
      flushParagraph();
      const body = [];
      while (i < lines.length && /^>\s?/.test(lines[i])) body.push(lines[i++].replace(/^>\s?/, ""));
      out.push(`<blockquote>${render(body.join("\n")).html}</blockquote>`);
      continue;
    }

    // Lists
    if (/^\s*([-*+]|\d+\.)\s+/.test(line)) {
      flushParagraph();
      const ordered = /^\s*\d+\./.test(line);
      const items = [];
      while (
        i < lines.length &&
        (/^\s*([-*+]|\d+\.)\s+/.test(lines[i]) || /^\s{2,}\S/.test(lines[i]))
      ) {
        if (/^\s*([-*+]|\d+\.)\s+/.test(lines[i])) {
          items.push([lines[i].replace(/^\s*([-*+]|\d+\.)\s+/, "")]);
        } else if (items.length) {
          items[items.length - 1].push(lines[i].trim());
        }
        i++;
      }
      const tag = ordered ? "ol" : "ul";
      out.push(`<${tag}>${items.map((it) => `<li>${inline(it.join(" "))}</li>`).join("")}</${tag}>`);
      continue;
    }

    if (line.trim() === "") {
      flushParagraph();
      i++;
      continue;
    }

    para.push(line.trim());
    i++;
  }
  flushParagraph();
  return { html: out.join("\n"), headings };
}

/* ------------------------------------------------------------------ */
/* Page assembly                                                       */
/* ------------------------------------------------------------------ */

function layout({ title, body, headings, current }) {
  const nav = site.nav
    .map((section) => {
      const links = section.items
        .map((item) => {
          const active = item.out === current ? ' class="active"' : "";
          return `<li><a href="${item.out}"${active}>${escapeHtml(item.title)}</a></li>`;
        })
        .join("");
      return `<div class="nav-section"><h4>${escapeHtml(section.title)}</h4><ul>${links}</ul></div>`;
    })
    .join("");

  const toc = headings.length
    ? `<nav class="toc"><h4>On this page</h4><ul>${headings
        .map((h) => `<li class="lvl-${h.level}"><a href="#${h.id}">${escapeHtml(h.title)}</a></li>`)
        .join("")}</ul></nav>`
    : "";

  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${escapeHtml(title)} - ${escapeHtml(site.title)}</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
<header class="topbar">
  <a class="brand" href="index.html">${escapeHtml(site.title)}</a>
  <span class="version">${escapeHtml(site.version)}</span>
  <a class="repo" href="${site.repo}" target="_blank" rel="noreferrer">GitHub</a>
</header>
<div class="layout">
  <aside class="sidebar">${nav}</aside>
  <main>${body}</main>
  ${toc}
</div>
<footer><p>${escapeHtml(site.footer)}</p></footer>
</body>
</html>
`;
}

/* ------------------------------------------------------------------ */
/* Build                                                               */
/* ------------------------------------------------------------------ */

rmSync(OUT, { recursive: true, force: true });
mkdirSync(OUT, { recursive: true });

const pages = site.nav.flatMap((s) => s.items);
const problems = [];
const produced = new Set();
let built = 0;

for (const page of pages) {
  const source = join(DOCS, page.src);
  if (!existsSync(source)) {
    problems.push(`missing source: ${page.src}`);
    continue;
  }
  const md = readFileSync(source, "utf8");
  const { html, headings } = render(md);
  const h1 = /^#\s+(.*)$/m.exec(md);
  writeFileSync(
    join(OUT, page.out),
    layout({
      title: h1 ? h1[1].trim() : page.title,
      body: html,
      headings,
      current: page.out,
    }),
  );
  produced.add(page.out);
  built++;
}

copyFileSync(join(DOCS, "theme", "style.css"), join(OUT, "style.css"));

// Every internal link must resolve to a page we actually produced.
for (const page of pages) {
  const target = join(OUT, page.out);
  if (!existsSync(target)) continue;
  const html = readFileSync(target, "utf8");
  for (const m of html.matchAll(/href="([^"]+)"/g)) {
    const href = m[1];
    if (/^(https?:|mailto:|#)/.test(href)) continue;
    const file = href.split("#")[0];
    if (!file || file === "style.css") continue;
    if (!produced.has(file)) problems.push(`${page.out}: broken link to ${file}`);
  }
}

if (problems.length) {
  console.error("Documentation build failed:");
  for (const p of [...new Set(problems)]) console.error(`  - ${p}`);
  process.exit(1);
}

console.log(`Built ${built} page${built === 1 ? "" : "s"} into ${OUT}`);
