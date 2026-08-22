// ---------------------------------------------------------------------------
// pil.js — the parts of Pillow the converter depends on, ported exactly.
//
// This is not "a Lanczos resize" or "a Floyd-Steinberg dither". It is
// Pillow 12.3.0's, transcribed from src/libImaging/Resample.c and
// Convert.c, down to the integer truncation and the fixed-point rounding.
// That is the whole point: tests/webtest.js asserts the .tbi this produces
// is byte-for-byte the .tbi toybox_slicer.py produces, and a resize that is
// merely "very close" fails that test on thousands of pixels, because a
// dither turns a one-level difference into a flipped bit.
//
// Do not "simplify" the arithmetic here. Every awkward line is load-bearing:
//   - C integer division truncates toward zero; JS Math.floor does not.
//   - Python's round() is round-half-to-even; JS Math.round is not.
//   - Pillow thresholds at l > 128, not >= 128.
// ---------------------------------------------------------------------------

// C's (int) cast on a double: truncate toward zero.
const trunc = Math.trunc;

// Python's round(): half away from zero is WRONG, it is half to even.
export function pyround(x) {
  const f = Math.floor(x);
  const d = x - f;
  if (d > 0.5) return f + 1;
  if (d < 0.5) return f;
  return (f % 2 === 0) ? f : f + 1;
}

// ---------------------------------------------------------------------------
// RGB -> L.  Convert.c: #define L24(rgb) (r*19595 + g*38470 + b*7471 + 0x8000)
// then >> 16.  ITU-R 601-2 luma in 16-bit fixed point.
// ---------------------------------------------------------------------------
export function rgbaToL(rgba, w, h) {
  const out = new Uint8Array(w * h);
  for (let j = 0, i = 0; j < w * h; j++, i += 4) {
    out[j] = (rgba[i] * 19595 + rgba[i + 1] * 38470 + rgba[i + 2] * 7471 + 0x8000) >>> 16;
  }
  return out;
}

// ---------------------------------------------------------------------------
// Lanczos resize, Resample.c.
// ---------------------------------------------------------------------------
const PRECISION_BITS = 32 - 8 - 2;          // 22
const LANCZOS_SUPPORT = 3.0;

function sinc(x) {
  if (x === 0.0) return 1.0;
  x = x * Math.PI;
  return Math.sin(x) / x;
}
function lanczos(x) {
  if (-3.0 <= x && x < 3.0) return sinc(x) * sinc(x / 3);
  return 0.0;
}

// precompute_coeffs(), then normalize_coeffs_8bpc() folded in.
function coeffs(inSize, outSize) {
  const scale = inSize / outSize;
  const filterscale = scale < 1.0 ? 1.0 : scale;
  const support = LANCZOS_SUPPORT * filterscale;
  const ksize = Math.ceil(support) * 2 + 1;
  const invfs = 1.0 / filterscale;

  const bounds = new Int32Array(outSize * 2);
  const kk = new Int32Array(outSize * ksize);
  const k = new Float64Array(ksize);

  for (let xx = 0; xx < outSize; xx++) {
    const center = (xx + 0.5) * scale;
    let xmin = trunc(center - support + 0.5);
    if (xmin < 0) xmin = 0;
    let xmax = trunc(center + support + 0.5);
    if (xmax > inSize) xmax = inSize;
    xmax -= xmin;

    let ww = 0.0;
    for (let x = 0; x < xmax; x++) {
      const w = lanczos((x + xmin - center + 0.5) * invfs);
      k[x] = w;
      ww += w;
    }
    const base = xx * ksize;
    for (let x = 0; x < xmax; x++) {
      const v = ww !== 0.0 ? k[x] / ww : k[x];
      // normalize_coeffs_8bpc: (int)(±0.5 + v * (1 << 22))
      const s = v * (1 << PRECISION_BITS);
      kk[base + x] = trunc(v < 0 ? s - 0.5 : s + 0.5);
    }
    for (let x = xmax; x < ksize; x++) kk[base + x] = 0;
    bounds[xx * 2] = xmin;
    bounds[xx * 2 + 1] = xmax;
  }
  return { ksize, bounds, kk };
}

const HALF = 1 << (PRECISION_BITS - 1);

function clip8(v) {
  v = v >> PRECISION_BITS;
  return v <= 0 ? 0 : (v > 255 ? 255 : v);
}

function resampleH(src, sw, sh, dw) {
  const { ksize, bounds, kk } = coeffs(sw, dw);
  const out = new Uint8Array(dw * sh);
  for (let yy = 0; yy < sh; yy++) {
    const ri = yy * sw, ro = yy * dw;
    for (let xx = 0; xx < dw; xx++) {
      const xmin = bounds[xx * 2], xmax = bounds[xx * 2 + 1], base = xx * ksize;
      let ss = HALF;
      for (let x = 0; x < xmax; x++) ss += src[ri + x + xmin] * kk[base + x];
      out[ro + xx] = clip8(ss);
    }
  }
  return out;
}

function resampleV(src, sw, sh, dh) {
  const { ksize, bounds, kk } = coeffs(sh, dh);
  const out = new Uint8Array(sw * dh);
  for (let yy = 0; yy < dh; yy++) {
    const ymin = bounds[yy * 2], ymax = bounds[yy * 2 + 1], base = yy * ksize;
    const ro = yy * sw;
    for (let xx = 0; xx < sw; xx++) {
      let ss = HALF;
      for (let y = 0; y < ymax; y++) ss += src[(y + ymin) * sw + xx] * kk[base + y];
      out[ro + xx] = clip8(ss);
    }
  }
  return out;
}

/** Image.resize((dw, dh), Image.LANCZOS) on an 8-bit single band. */
export function resizeLanczos(src, sw, sh, dw, dh) {
  // Pillow short-circuits an identity resize to a copy; matching that keeps
  // a picture that is already 480 wide bit-identical instead of ±1.
  if (dw === sw && dh === sh) return src.slice();
  let img = src, w = sw;
  if (dw !== sw) { img = resampleH(img, sw, sh, dw); w = dw; }
  if (dh !== sh) { img = resampleV(img, w, sh, dh); }
  return img;
}

// ---------------------------------------------------------------------------
// Floyd-Steinberg, Convert.c tobilevel().
//
// The error is carried at 16x in `l` and in the `errors` row buffer, so the
// only division is the one that reads them back. C's / truncates toward zero
// and the threshold is strictly greater than 128 — both matter.
// ---------------------------------------------------------------------------
export function ditherFS(g, w, h) {
  const out = new Uint8Array(w * h);
  const errors = new Int32Array(w + 1);
  for (let y = 0; y < h; y++) {
    let l = 0, l0 = 0, l1 = 0, l2, d2;
    const ri = y * w;
    for (let x = 0; x < w; x++) {
      const t = l + errors[x + 1];
      // C: (l + errors[x+1]) / 16, truncating toward zero.
      const q = t < 0 ? -((-t) >> 4) : (t >> 4);
      let v = g[ri + x] + q;
      l = v <= 0 ? 0 : (v < 256 ? v : 255);
      const o = l > 128 ? 255 : 0;
      out[ri + x] = o;
      l -= o;
      l2 = l; d2 = l + l;
      l += d2; errors[x] = l + l0;
      l += d2; l0 = l + l1; l1 = l2;
      l += d2;
    }
    errors[w] = l0;
  }
  return out;
}

// ---------------------------------------------------------------------------
// Serpentine Floyd-Steinberg — toybox_slicer.py _serpentine(), levels = 2.
//
// The order the three downward shifts are added in (1/16, 5/16, 3/16) is not
// cosmetic: float addition is not associative, and the Python original
// reached its targets in that order under both scan directions. Change the
// order and pixels move.
// ---------------------------------------------------------------------------
export function ditherSerpentine(g, w, h) {
  const step = 255.0;                         // 255 / (levels - 1), levels = 2
  const out = new Uint8Array(w * h);
  let cur = new Float64Array(w);
  for (let x = 0; x < w; x++) cur[x] = g[x];
  const err = new Float64Array(w);

  for (let y = 0; y < h; y++) {
    let nxt = null;
    if (y + 1 < h) {
      nxt = new Float64Array(w);
      const ri = (y + 1) * w;
      for (let x = 0; x < w; x++) nxt[x] = g[ri + x];
    }
    const ro = y * w;
    const d = (y % 2 === 0) ? 1 : -1;
    const start = d === 1 ? 0 : w - 1;
    const end = d === 1 ? w : -1;
    for (let x = start; x !== end; x += d) {
      const old = cur[x];
      let nv = pyround(old / step) * step;
      if (nv < 0.0) nv = 0.0; else if (nv > 255.0) nv = 255.0;
      out[ro + x] = nv | 0;
      const e = old - nv;
      err[x] = e;
      const nx = x + d;
      if (nx >= 0 && nx < w) cur[nx] += e * 0.4375;
    }
    if (nxt === null) break;
    if (d === 1) {
      for (let x = 1; x < w; x++) nxt[x] += err[x - 1] * 0.0625;
      for (let x = 0; x < w; x++) nxt[x] += err[x] * 0.3125;
      for (let x = 0; x < w - 1; x++) nxt[x] += err[x + 1] * 0.1875;
    } else {
      for (let x = 0; x < w - 1; x++) nxt[x] += err[x + 1] * 0.0625;
      for (let x = 0; x < w; x++) nxt[x] += err[x] * 0.3125;
      for (let x = 1; x < w; x++) nxt[x] += err[x - 1] * 0.1875;
    }
    cur = nxt;
  }
  return out;
}

/** dither = "none": a plain threshold, for text plates. */
export function ditherNone(g, w, h) {
  const out = new Uint8Array(w * h);
  for (let i = 0; i < w * h; i++) out[i] = g[i] >= 128 ? 255 : 0;
  return out;
}

export function dither1bit(g, w, h, mode) {
  if (mode === "none") return ditherNone(g, w, h);
  if (mode === "serpentine") return ditherSerpentine(g, w, h);
  return ditherFS(g, w, h);
}
