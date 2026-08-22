// ---------------------------------------------------------------------------
// convert.js — the whole job: epub in, epub with toybox/*.tbi out.
//
// Mirrors add_epub_art() in toybox_slicer.py, including the rule that every
// original entry is written back with its own data, mimetype stays first and
// stored, and the only new entries are under toybox/.
// ---------------------------------------------------------------------------

import { readZip, readEntry, writeZip, crc32 } from "./zip.js";
import { epubImageEntries } from "./epub.js";
import { artEntryName, TBI_FILE_SIZE } from "./tbi.js";

const MIME = {
  jpg: "image/jpeg", jpeg: "image/jpeg", png: "image/png", gif: "image/gif",
  webp: "image/webp", bmp: "image/bmp", avif: "image/avif",
};
const IMG_EXT = /\.(jpe?g|png|gif|webp|bmp|avif)$/i;

function mimeFor(name) {
  const m = /\.([a-z0-9]+)$/i.exec(name);
  return m ? (MIME[m[1].toLowerCase()] || "") : "";
}

class Pool {
  constructor(n) {
    this.workers = [];
    for (let i = 0; i < n; i++) {
      this.workers.push(new Worker(new URL("./worker.js", import.meta.url), { type: "module" }));
    }
    this.idle = [...this.workers];
    this.queue = [];
    this.seq = 0;
    for (const w of this.workers) {
      w.onmessage = (ev) => {
        const done = w._resolve;
        w._resolve = null;
        this.idle.push(w);
        this._pump();
        done(ev.data);
      };
    }
  }
  run(msg, transfer) {
    return new Promise((resolve) => {
      this.queue.push({ msg, transfer, resolve });
      this._pump();
    });
  }
  _pump() {
    while (this.queue.length && this.idle.length) {
      const w = this.idle.pop();
      const job = this.queue.shift();
      w._resolve = job.resolve;
      w.postMessage(job.msg, job.transfer);
    }
  }
  close() { for (const w of this.workers) w.terminate(); }
}

/**
 * @param bytes   the epub, as a Uint8Array
 * @param opts    {trim, turn, dither, workers}
 * @param onProgress  ({done, total, name}) => void
 */
export async function convertEpub(bytes, opts = {}, onProgress = () => {}) {
  const entries = readZip(bytes);
  const byName = new Map(entries.map((e) => [e.name, e]));
  const names = new Set(byName.keys());

  // The spine and every XHTML document have to be decompressed to be read;
  // nothing else does.
  const textCache = new Map();
  const needText = [...names].filter((n) => /\.(opf|xhtml|html|htm|xml)$/i.test(n));
  for (const n of needText) {
    try {
      textCache.set(n, new TextDecoder("utf-8").decode(await readEntry(byName.get(n))));
    } catch { /* a document we cannot read is a document with no pictures */ }
  }
  const read = (n) => textCache.get(n) ?? null;

  let wanted = epubImageEntries(names, read);
  if (!wanted.length) {
    // A book whose spine we could not follow still has pictures in it.
    wanted = [...names].filter((n) => IMG_EXT.test(n) && !n.startsWith("toybox/"));
    wanted.sort();
  }

  // SVG is not a failure, it is a thing the device cannot draw — the desktop
  // app hits the same wall from the other side, its decoder refusing the file.
  // Separating the two keeps a book that converted perfectly from reporting
  // in red.
  // The first picture in reading order is the cover, and a cover is not
  // trimmed: everywhere else a flat border is dead space, but on a cover it
  // is the design — the band the title sits in, the frame the artist drew.
  //
  // Taken BEFORE the SVG split, deliberately. add_epub_art() in
  // toybox_slicer.py has no SVG list — it tries every entry and lets the
  // decoder refuse — so its first entry includes one. Skipping SVGs here
  // first would make the two disagree about which picture is the cover on
  // exactly one kind of book, and the byte-identity test only fails on that
  // book if someone thinks to build it. Same list, same index, no surprise.
  const coverEntry = wanted.length ? wanted[0] : null;

  const svg = wanted.filter((n) => /\.svgz?$/i.test(n));
  wanted = wanted.filter((n) => !/\.svgz?$/i.test(n));

  const nCores = opts.workers || Math.max(1, Math.min(16, (navigator.hardwareConcurrency || 4) - 1));
  const pool = new Pool(Math.min(nCores, Math.max(1, wanted.length)));
  const art = new Map();
  let done = 0, failed = 0, trimmed = 0, turned = 0;
  const problems = [];
  for (const n of svg) problems.push(`${n} — skipped, the device cannot draw an SVG`);

  try {
    await Promise.all(wanted.map(async (entry) => {
      const raw = await readEntry(byName.get(entry));
      const copy = new Uint8Array(raw);      // detachable: the pool transfers it
      const entryOpts = entry === coverEntry ? { ...opts, trim: false } : opts;
      const res = await pool.run(
        { id: entry, bytes: copy, type: mimeFor(entry), opts: entryOpts },
        [copy.buffer]
      );
      done++;
      if (res.ok && res.tbi && res.tbi.length === TBI_FILE_SIZE) {
        art.set(artEntryName(entry), res.tbi);
        if (res.trimmed) trimmed++;
        if (res.turned) turned++;
      } else {
        failed++;
        problems.push(`${entry}: ${res.error || "unexpected size"}`);
      }
      onProgress({ done, total: wanted.length, name: entry });
    }));
  } finally {
    pool.close();
  }

  // Rebuild: every original entry copied through untouched, ours appended.
  const records = [];
  for (const e of entries) {
    if (e.name.startsWith("toybox/")) continue;     // ours from a previous run
    records.push({
      name: e.name, nameBytes: e.nameBytes, flags: e.flags,
      method: e.name === "mimetype" ? 0 : e.method,
      time: e.time, date: e.date, crc: e.crc, usize: e.usize,
      extAttr: e.extAttr, raw: e.raw,
    });
  }
  if (records.length && records[0].name !== "mimetype") {
    problems.push("note: 'mimetype' is not this book's first entry — copying the order as found");
  }
  // mimetype must be stored; if the source had it deflated we would have to
  // inflate it, which is the one entry we ever rewrite.
  const mt = entries.find((e) => e.name === "mimetype");
  if (mt && mt.method !== 0) {
    const plain = await readEntry(mt);
    const r = records.find((x) => x.name === "mimetype");
    r.raw = plain; r.method = 0; r.crc = crc32(plain); r.usize = plain.length;
  }
  for (const name of [...art.keys()].sort()) {
    const data = art.get(name);
    records.push({ name, method: 0, crc: crc32(data), usize: data.length, raw: data });
  }

  return {
    blob: writeZip(records),
    prepared: art.size, failed, skipped: svg.length, cover: coverEntry,
    total: wanted.length, trimmed, turned, problems,
  };
}
