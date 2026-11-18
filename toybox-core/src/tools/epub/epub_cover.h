// EPUB cover -> the two pictures book_thumbs.h keeps: the 480x800 one-bit
// loading-screen cover on the card, and the 96x160 strip thumbnail in flash.
// Decoded by the illustration pipeline (epub_art.h), so a cover looks the
// way the book's own pictures do.
#pragma once
#include "../tools_ui.h"
#include "epubcore.h"

namespace epubcov {

// Decodes the open book's cover and saves the thumbnail under `bookFile`.
// False on any failure (no cover, undecodable, out of memory); the caller
// records the failure so it is never retried.
// `transient` comes back true when the cover was not built for a reason that
// says nothing about the book -- no memory for the decode, a card that stopped
// answering. The caller must NOT mark those permanently: a device briefly
// short of heap would otherwise decide, once and for ever, that a perfectly
// good cover cannot be decoded.
bool makeThumb(ToolsHost& host, epubc::Book& book, const char* bookFile,
               bool* transient = nullptr);

}  // namespace epubcov
