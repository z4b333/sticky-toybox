// Choosing a reading face off the SD card.
//
// The card holds families in CrossInk's layout -- /.fonts/<Family>/, one
// .cpfont per size -- and this picks which of a family's sizes belongs in each
// of Toybox's four line boxes, reads those files into PSRAM, and hands the
// bytes to gfx (see gfx.h, cardFaceSet).
//
// Their sizes are points at 150 DPI and ours are pixel boxes, so nothing lines
// up by name: their 8 is a 20 px line and their 18 is a 45 px one. What lines
// up is the number each file states in its own header, and that is what gets
// compared.
//
// The fonts stay on the card. They are megabytes each, the card is where they
// arrive, and copying them into a 4.9 MB filesystem shared with somebody's
// notes to save a re-read at boot is a poor trade. The cost is that a device
// with the card out reads in its baked face, which is the honest behaviour --
// the same as a device with the card out has no books.
#pragma once
#include <stdint.h>

namespace cardfonts {

// Up to this many families offered, and this many sizes considered inside one.
inline constexpr int MAX_FAMILIES = 24;
inline constexpr int MAX_SIZES = 16;

// What the card has. -1 when no card answered, 0 when it has no fonts on it.
// Claims the bus itself, so the caller must expect a full refresh afterwards.
int families(char names[][32], int max);

// Load a family: read every size's header, pick one per box, read those whole
// into PSRAM and install them. False leaves whatever was loaded before alone,
// so a bad card cannot take away the face that was working a moment ago.
//
// Two sets, for the device's two kinds of text. UNIVERSAL is the firmware's
// own face -- menus, labels, the hub. CONTENT belongs to one app and covers
// what the owner put there, so a novel can be read in a serif without the
// settings page changing clothes.
//
// Only one content family is resident at a time: they are megabytes, and the
// app that opens next wants its own. Leaving an app drops it.
// `bodyLine` asks for a particular size in the body box -- the line height
// wanted, in pixels, nearest match wins -- so that a reader can be set in the
// family's own 12 pt rather than in whichever of its files happened to land
// closest to the firmware's 24 px box. 0 leaves every box chosen the automatic
// way, which is what every app but the reader wants.
bool useUniversal(const char* family);
bool useContent(const char* family, int bodyLine = 0);

// Put the baked faces back and free the card's.
void noneUniversal();
void noneContent();

// The family currently drawn from, or "" for the baked faces.
const char* universal();
const char* content();

// The sizes the loaded content family ships, as the line heights its files
// state, smallest first and one entry per distinct height. Only the ones a
// book can be read at: a family's 8 pt is a 20 px line, smaller than the body
// size the whole firmware settled on, and offering it would be offering a page
// nobody can read.
//
// Zero when the content face is baked, or when the family ships only one
// usable size -- in both cases there is nothing to step through and the caller
// keeps its own sizes.
int contentSizeCount();
int contentSizeLine(int i);
// The size number the file itself is named for -- CrossInk's points at 150 DPI,
// which is what the person picking a file off their card sees. 0 when the name
// carried no number.
int contentSizePt(int i);
// Which of them the body box is set in, or -1 when the boxes were filled the
// automatic way.
int contentSizeIndex();

}  // namespace cardfonts
