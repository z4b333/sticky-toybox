// ---------------------------------------------------------------------------
// epub.js — which pictures the reader will actually put on a page.
//
// Mirrors epub_spine() and epub_image_entries() in toybox_slicer.py. The OPF
// is read with regexes rather than a DOM parser so this file works unchanged
// inside a Worker, where DOMParser does not exist.
// ---------------------------------------------------------------------------

const IMG_RE = /<(?:img|image)\b([^>]*?)\/?>/gis;
const SRC_RE = /(?:\bsrc|xlink:href|\bhref)\s*=\s*["']([^"']+)["']/i;

/** os.path.normpath on a forward-slash path. */
export function normpath(p) {
  const absolute = p.startsWith("/");
  const out = [];
  for (const part of p.split("/")) {
    if (part === "" || part === ".") continue;
    if (part === "..") {
      if (out.length && out[out.length - 1] !== "..") out.pop();
      else if (!absolute) out.push("..");
    } else out.push(part);
  }
  const s = out.join("/");
  return absolute ? "/" + s : (s || ".");
}

export function dirname(p) {
  const i = p.lastIndexOf("/");
  return i < 0 ? "" : p.slice(0, i);
}

function join(dir, href) {
  if (href.startsWith("/")) return href;
  return dir ? dir + "/" + href : href;
}

/** urllib.parse.unquote, tolerant of stray percent signs. */
export function unquote(s) {
  return s.replace(/%[0-9a-fA-F]{2}/g, (m) => {
    try { return decodeURIComponent(m); } catch { return m; }
  });
}

function attr(tag, name) {
  const m = new RegExp(`\\b${name}\\s*=\\s*["']([^"']*)["']`, "i").exec(tag);
  return m ? m[1] : null;
}

/** The reading order, as hrefs inside the archive. */
export function epubSpine(names, read) {
  let opf = null;
  try {
    const container = read("META-INF/container.xml");
    if (container) {
      const m = /<rootfile\b[^>]*>/i.exec(container);
      if (m) opf = attr(m[0], "full-path");
    }
  } catch { opf = null; }
  if (!opf || !names.has(opf)) {
    opf = [...names].find((n) => n.toLowerCase().endsWith(".opf")) || null;
  }
  if (!opf) return { spine: [], base: "" };
  const base = dirname(opf);

  let xml;
  try { xml = read(opf); } catch { return { spine: [], base }; }
  if (!xml) return { spine: [], base };

  const items = new Map();
  for (const m of xml.matchAll(/<item\b[^>]*>/gi)) {
    const id = attr(m[0], "id");
    if (id) items.set(id, attr(m[0], "href") || "");
  }
  const order = [];
  for (const m of xml.matchAll(/<itemref\b[^>]*>/gi)) {
    const idref = attr(m[0], "idref");
    if (idref) order.push(idref);
  }

  const out = [];
  for (const idref of order) {
    const href = items.get(idref);
    if (!href) continue;
    // Not unquoted — toybox_slicer.py does not unquote the manifest href
    // either, and the two must agree on what counts as "in the archive".
    const full = normpath(join(base, href));
    if (names.has(full)) out.push(full);
  }
  if (!out.length) {
    for (const n of names) {
      if (/\.(xhtml|html|htm)$/i.test(n)) out.push(n);
    }
    out.sort();
  }
  return { spine: out, base };
}

/**
 * Every image the reader will put on a page, as resolved entry names, in
 * reading order and without repeats.
 */
export function epubImageEntries(names, read) {
  const { spine } = epubSpine(names, read);
  const out = [], seen = new Set();
  for (const doc of spine) {
    let html;
    try { html = read(doc); } catch { continue; }
    if (html == null) continue;
    const docdir = dirname(doc);
    IMG_RE.lastIndex = 0;
    for (const hit of html.matchAll(IMG_RE)) {
      const src = SRC_RE.exec(hit[1]);
      if (!src) continue;
      const href = unquote(src[1].split("#")[0]);
      const entry = normpath(join(docdir, href));
      if (names.has(entry) && !seen.has(entry)) { seen.add(entry); out.push(entry); }
    }
  }
  return out;
}
