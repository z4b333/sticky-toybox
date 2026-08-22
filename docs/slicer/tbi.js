// ---------------------------------------------------------------------------
// tbi.js — the picture pipeline: trim, turn, fit, dither, pack.
//
// Mirrors add_epub_art() / tbi_canvas() / make_tbi() in toybox_slicer.py.
// ---------------------------------------------------------------------------

import { rgbaToL, resizeLanczos, dither1bit, pyround } from "./pil.js";

export const TBK_W = 480;
export const TBK_H = 800;
export const TBI_BYTES = TBK_W * TBK_H / 8;      // 48,000
export const TBI_FILE_SIZE = 8 + TBI_BYTES;      // 48,008
export const ART_WIDE = 1.15;

/** A single-band 8-bit image. */
export function gray(data, w, h) { return { data, w, h }; }

/** How much of the panel this picture covers once fitted whole. */
export function fitGain(im) {
  const s = Math.min(TBK_W / im.w, TBK_H / im.h);
  return (im.w * s) * (im.h * s) / (TBK_W * TBK_H);
}

/** np.median of a flat Int array, matching numpy's average-of-middle-two. */
function median(arr) {
  const a = Float64Array.from(arr).sort();
  const n = a.length;
  if (n === 0) return 0;
  return (n % 2) ? a[(n - 1) / 2] : (a[n / 2 - 1] + a[n / 2]) / 2;
}

function crop(im, l, t, r, b) {
  const w = r - l, h = b - t;
  const out = new Uint8Array(w * h);
  for (let y = 0; y < h; y++) {
    out.set(im.data.subarray((t + y) * im.w + l, (t + y) * im.w + r), y * w);
  }
  return gray(out, w, h);
}

/**
 * trim_border() — crop the flat border baked into a picture, whatever colour.
 * Returns the same object when there is nothing worth taking, so the caller
 * can use identity to tell whether a trim happened, exactly as Python's
 * `cut is not im` does.
 */
export function trimBorder(im, tol = 6, pad = 2, minSide = 0.15) {
  const { data: a, w, h } = im;
  if (a.length === 0 || Math.min(w, h) < 4) return im;

  const ring = new Uint8Array(w * 2 + h * 2);
  let k = 0;
  for (let x = 0; x < w; x++) ring[k++] = a[x];
  for (let x = 0; x < w; x++) ring[k++] = a[(h - 1) * w + x];
  for (let y = 0; y < h; y++) ring[k++] = a[y * w];
  for (let y = 0; y < h; y++) ring[k++] = a[y * w + w - 1];
  const base = Math.trunc(median(ring));      // int(np.median(...))

  let t = -1, b = -1, l = -1, r = -1;
  const rowHit = new Uint8Array(h), colHit = new Uint8Array(w);
  let any = false;
  for (let y = 0; y < h; y++) {
    const ri = y * w;
    for (let x = 0; x < w; x++) {
      if (Math.abs(a[ri + x] - base) > tol) { rowHit[y] = 1; colHit[x] = 1; any = true; }
    }
  }
  if (!any) return im;                        // the whole picture is one colour
  for (let y = 0; y < h; y++) if (rowHit[y]) { if (t < 0) t = y; b = y + 1; }
  for (let x = 0; x < w; x++) if (colHit[x]) { if (l < 0) l = x; r = x + 1; }

  l = Math.max(0, l - pad); t = Math.max(0, t - pad);
  r = Math.min(w, r + pad); b = Math.min(h, b + pad);
  if ((r - l) < w * minSide || (b - t) < h * minSide) return im;   // a sliver
  if ((r - l) >= w - 2 && (b - t) >= h - 2) return im;             // nothing to take
  return crop(im, l, t, r, b);
}

/** rotate(90, expand=True) — anticlockwise, matching the manga spread page. */
export function rotate90ccw(im) {
  const { data: a, w, h } = im;
  const out = new Uint8Array(w * h);
  // destination is h wide, w tall; dst(x', y') = src(x = h-1-y', y = ... )
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      out[(w - 1 - x) * h + y] = a[y * w + x];
    }
  }
  return gray(out, h, w);
}

/**
 * tbi_canvas() with adjust=None and stretch=False — the 480x800 greyscale the
 * panel is handed. `fit` letterboxes the whole picture; without it the picture
 * fills and the edges are cropped.
 */
export function tbiCanvas(im, fit = true) {
  const scale = fit
    ? Math.min(TBK_W / im.w, TBK_H / im.h)
    : Math.max(TBK_W / im.w, TBK_H / im.h);
  const nw = Math.max(1, pyround(im.w * scale));
  const nh = Math.max(1, pyround(im.h * scale));
  const small = resizeLanczos(im.data, im.w, im.h, nw, nh);

  const canvas = new Uint8Array(TBK_W * TBK_H).fill(255);
  const ox = Math.floor((TBK_W - nw) / 2), oy = Math.floor((TBK_H - nh) / 2);
  for (let y = 0; y < nh; y++) {
    const dy = oy + y;
    if (dy < 0 || dy >= TBK_H) continue;
    for (let x = 0; x < nw; x++) {
      const dx = ox + x;
      if (dx < 0 || dx >= TBK_W) continue;
      canvas[dy * TBK_W + dx] = small[y * nw + x];
    }
  }
  return gray(canvas, TBK_W, TBK_H);
}

/** np.packbits(..., bitorder="big"): 1 = white, MSB first. */
export function packbits(bw) {
  const out = new Uint8Array(TBI_BYTES);
  for (let i = 0, j = 0; i < bw.length; i += 8, j++) {
    out[j] = (bw[i] ? 128 : 0) | (bw[i + 1] ? 64 : 0) | (bw[i + 2] ? 32 : 0)
           | (bw[i + 3] ? 16 : 0) | (bw[i + 4] ? 8 : 0) | (bw[i + 5] ? 4 : 0)
           | (bw[i + 6] ? 2 : 0) | (bw[i + 7] ? 1 : 0);
  }
  return out;
}

/** make_tbi() — the 48,008 bytes, header and all. */
export function makeTbi(im, { fit = true, dither = "fs" } = {}) {
  const canvas = tbiCanvas(im, fit);
  const bw = dither1bit(canvas.data, TBK_W, TBK_H, dither);
  const bits = packbits(bw);
  const out = new Uint8Array(TBI_FILE_SIZE);
  out[0] = 0x54; out[1] = 0x42; out[2] = 0x49; out[3] = 0x31;   // "TBI1"
  out[4] = TBK_W & 0xff; out[5] = TBK_W >> 8;                   // little endian
  out[6] = TBK_H & 0xff; out[7] = TBK_H >> 8;
  out.set(bits, 8);
  return out;
}

/**
 * The whole per-picture path from decoded RGBA, matching add_epub_art():
 * trim the border, turn a wide plate, then fit it whole on white.
 */
export function prepareArt(rgba, w, h, opts = {}) {
  const { trim = true, turn = true, dither = "fs" } = opts;
  let pic = gray(rgbaToL(rgba, w, h), w, h);
  let trimmed = false, turned = false;
  if (trim) {
    const cut = trimBorder(pic);
    if (cut !== pic) { pic = cut; trimmed = true; }
  }
  if (turn && pic.w > pic.h * ART_WIDE) { pic = rotate90ccw(pic); turned = true; }
  return { tbi: makeTbi(pic, { fit: true, dither }), trimmed, turned };
}

/** `OEBPS/Images/a.b.jpg` -> `toybox/OEBPS/Images/a.b.tbi`. */
export function artEntryName(entry) {
  const i = entry.lastIndexOf(".");
  const j = entry.lastIndexOf("/");
  const stem = (i > j) ? entry.slice(0, i) : entry;
  return "toybox/" + stem + ".tbi";
}
