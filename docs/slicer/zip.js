// ---------------------------------------------------------------------------
// zip.js — just enough ZIP to read an EPUB and write it back unharmed.
//
// The writer does NOT recompress. It copies each original entry's compressed
// bytes verbatim, so the book that comes out is the book that went in, and a
// 15 MB novel rewrites in milliseconds instead of seconds. The only new
// entries are the stored toybox/*.tbi.
// ---------------------------------------------------------------------------

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320 ^ (c >>> 1)) : (c >>> 1);
    t[n] = c >>> 0;
  }
  return t;
})();

export function crc32(buf) {
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

const EOCD_SIG = 0x06054b50;
const CD_SIG = 0x02014b50;

/**
 * Parse the central directory. Returns entries in directory order, each with
 * the raw compressed slice so it can be copied straight through.
 */
export function readZip(bytes) {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let eocd = -1;
  for (let i = bytes.length - 22; i >= 0 && i >= bytes.length - 22 - 65535; i--) {
    if (dv.getUint32(i, true) === EOCD_SIG) { eocd = i; break; }
  }
  if (eocd < 0) throw new Error("not a zip file (no end-of-central-directory)");
  const count = dv.getUint16(eocd + 10, true);
  let p = dv.getUint32(eocd + 16, true);

  const entries = [];
  for (let n = 0; n < count; n++) {
    if (dv.getUint32(p, true) !== CD_SIG) throw new Error("central directory is corrupt");
    const flags = dv.getUint16(p + 8, true);
    const method = dv.getUint16(p + 10, true);
    const time = dv.getUint16(p + 12, true);
    const date = dv.getUint16(p + 14, true);
    const crc = dv.getUint32(p + 16, true);
    const csize = dv.getUint32(p + 20, true);
    const usize = dv.getUint32(p + 24, true);
    const nlen = dv.getUint16(p + 28, true);
    const elen = dv.getUint16(p + 30, true);
    const clen = dv.getUint16(p + 32, true);
    const extAttr = dv.getUint32(p + 38, true);
    const lho = dv.getUint32(p + 42, true);
    const nameBytes = bytes.subarray(p + 46, p + 46 + nlen);
    const name = new TextDecoder(flags & 0x800 ? "utf-8" : "utf-8").decode(nameBytes);

    // The local header's own name/extra lengths are what actually locate the data.
    const lnlen = dv.getUint16(lho + 26, true);
    const lelen = dv.getUint16(lho + 28, true);
    const dataStart = lho + 30 + lnlen + lelen;

    entries.push({
      name, nameBytes, flags, method, time, date, crc, csize, usize, extAttr,
      raw: bytes.subarray(dataStart, dataStart + csize),
    });
    p += 46 + nlen + elen + clen;
  }
  return entries;
}

/** Decompress one entry to bytes. */
export async function readEntry(e) {
  if (e.method === 0) return e.raw;
  if (e.method !== 8) throw new Error(`${e.name}: compression method ${e.method} not supported`);
  const ds = new DecompressionStream("deflate-raw");
  const stream = new Blob([e.raw]).stream().pipeThrough(ds);
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

/**
 * Write a zip from a list of {name, method, crc, usize, raw} records.
 * `raw` is already-compressed data when method is 8, plain bytes when 0.
 */
export function writeZip(records) {
  const enc = new TextEncoder();
  const parts = [];
  const central = [];
  let offset = 0;

  for (const r of records) {
    const nameBytes = r.nameBytes || enc.encode(r.name);
    const lh = new Uint8Array(30 + nameBytes.length);
    const dv = new DataView(lh.buffer);
    dv.setUint32(0, 0x04034b50, true);
    dv.setUint16(4, 20, true);
    // Bit 3 (data descriptor) is cleared: we know crc and sizes up front, so
    // the local header carries them and no descriptor follows.
    dv.setUint16(6, (r.flags || 0) & ~0x08, true);
    dv.setUint16(8, r.method, true);
    dv.setUint16(10, r.time || 0, true);
    dv.setUint16(12, r.date || 0x21, true);
    dv.setUint32(14, r.crc, true);
    dv.setUint32(18, r.raw.length, true);
    dv.setUint32(22, r.usize, true);
    dv.setUint16(26, nameBytes.length, true);
    dv.setUint16(28, 0, true);
    lh.set(nameBytes, 30);

    const cd = new Uint8Array(46 + nameBytes.length);
    const cv = new DataView(cd.buffer);
    cv.setUint32(0, CD_SIG, true);
    cv.setUint16(4, 20, true);
    cv.setUint16(6, 20, true);
    cv.setUint16(8, (r.flags || 0) & ~0x08, true);
    cv.setUint16(10, r.method, true);
    cv.setUint16(12, r.time || 0, true);
    cv.setUint16(14, r.date || 0x21, true);
    cv.setUint32(16, r.crc, true);
    cv.setUint32(20, r.raw.length, true);
    cv.setUint32(24, r.usize, true);
    cv.setUint16(28, nameBytes.length, true);
    cv.setUint32(38, r.extAttr || 0, true);
    cv.setUint32(42, offset, true);
    cd.set(nameBytes, 46);

    parts.push(lh, r.raw);
    central.push(cd);
    offset += lh.length + r.raw.length;
  }

  const cdStart = offset;
  let cdSize = 0;
  for (const c of central) cdSize += c.length;
  const eocd = new Uint8Array(22);
  const ev = new DataView(eocd.buffer);
  ev.setUint32(0, EOCD_SIG, true);
  ev.setUint16(8, central.length, true);
  ev.setUint16(10, central.length, true);
  ev.setUint32(12, cdSize, true);
  ev.setUint32(16, cdStart, true);

  return new Blob([...parts, ...central, eocd], { type: "application/epub+zip" });
}
