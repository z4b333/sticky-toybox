// EPUB cover -> 96x160 thumbnail, into the same flash store the .tbk covers
// use. JPEG through the vendored TJpgDec (grayscale output, hardware-scale
// 1/2..1/8 for big covers), PNG through the tinfl-backed decoder in
// epub_png.h. Every source pixel is AVERAGED into its target cell before an
// ordered dither -- the same no-sampling rule every thumbnail here follows.
#pragma once
#include "../tools_ui.h"
#include "epubcore.h"

namespace epubcov {

// Decodes the open book's cover and saves the thumbnail under `bookFile`.
// False on any failure (no cover, undecodable, out of memory); the caller
// records the failure so it is never retried.
// `transient` comes back true when the cover was not built for a reason that
// says nothing about the book -- no memory for the bands, a card that stopped
// answering. The caller must NOT mark those permanently: a device briefly
// short of heap would otherwise decide, once and for ever, that a perfectly
// good cover cannot be decoded.
bool makeThumb(ToolsHost& host, epubc::Book& book, const char* bookFile,
               bool* transient = nullptr);

}  // namespace epubcov
