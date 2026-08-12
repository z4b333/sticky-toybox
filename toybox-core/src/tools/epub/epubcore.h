// The EPUB core: everything about reading an .epub that does not touch a
// screen or an SD card directly.
//
// An EPUB is a zip of XHTML chapters plus two small XML files that say what
// order the chapters go in. This core reads the zip through a caller-supplied
// byte source, walks container.xml -> content.opf -> spine, and streams one
// chapter at a time as a flat sequence of words. No DOM, no CSS, no images:
// the reader above lays the words into pages with the device fonts.
//
// The one deliberate complication is CrossPoint compatibility. CrossPoint
// Reader keeps its reading position on the card as a visible-codepoint offset
// into the chapter (progress.bin, see the sidecar section below), so a card
// moved between the two firmwares keeps its place. That only works if both
// firmwares count codepoints identically, which pins several choices here to
// CrossPoint's parser: what counts as visible text (everything inside <body>
// except head/style/script/title/rp subtrees, whitespace included), and which
// named entities expand (the vendored table in epub_entities.h).
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace epubc {

// The byte source for one open .epub. The host owns the file and the SD bus;
// this interface is all the core ever sees, which is also what lets the
// preview harness feed it a zip from memory.
struct IO {
  virtual int read(uint32_t pos, void* dst, uint32_t n) = 0;  // bytes read, <0 on error
  virtual uint32_t size() = 0;
  virtual ~IO() = default;
};

inline constexpr int MAX_SPINE = 300;   // chapters; the largest real books run ~150
inline constexpr int WORD_CAP = 96;     // bytes per emitted word; longer words split

// What Book::next() hands back.
enum Token : int {
  TOK_END = 0,    // chapter finished
  TOK_WORD = 1,   // a word (whitespace-delimited run) and its start offset
  TOK_PARA = 2,   // a paragraph boundary
  // An illustration. `word` comes back holding the image's zip entry name, and
  // the offset is unchanged -- an <img> carries no character data, so it adds
  // nothing to the count CrossPoint shares. That is the whole reason pictures
  // could be added to this reader at all without moving anybody's bookmark.
  TOK_IMAGE = 3,
  TOK_ERR = -1,
};

class Book {
 public:
  ~Book() { close(); }

  // Parses the zip directory, container.xml and the OPF, and resolves every
  // spine chapter to its zip entry. On failure error() says why, briefly.
  bool open(IO& io);
  void close();

  int spineCount() const { return _spineN; }
  const char* error() const { return _err; }

  // How large each chapter's XHTML is uncompressed, and the book's total.
  // The zip's central directory already said, so a position can be placed as
  // a fraction of the whole book without decompressing anything -- which is
  // what the KOReader sidecar needs and what nothing else here has cause to
  // ask. Bytes of markup, not of text, so the fraction is approximate in the
  // way a chapter heavy with tags is approximate; across a whole book the
  // error is small and it never accumulates, because every chapter boundary
  // is exact.
  uint32_t spineBytes(int i) const;
  uint32_t spineTotalBytes() const;

  // Chapter streaming. One chapter open at a time; reopening the same index
  // restarts it from the top (that is how the reader pages backwards).
  bool chapterOpen(int spineIdx);
  void chapterClose();

  // The word stream. `word` receives UTF-8 (NUL-terminated, at most WORD_CAP
  // bytes); `startOff` the visible-codepoint offset of the word's first
  // codepoint, in CrossPoint's counting.
  int next(char* word, uint32_t& startOff);

  // Reads an arbitrary zip entry by name -- the pre-rendered artwork beside an
  // image, or the image itself. Same one-at-a-time rule as the cover: no
  // chapter may be open across it.
  // The full entry name behind the last TOK_IMAGE. `next()` also copies it
  // into `word`, but WORD_CAP is 96 bytes and a real image path can be longer,
  // so anything that means to open it should ask here.
  const char* imageName() const { return _imgName; }

  bool blobOpen(const char* entryName);
  int blobRead(uint8_t* dst, int n) { return entryRead(dst, n); }
  void blobClose() { entryClose(); }
  uint32_t blobSize() const { return _blob.usize; }

  // The cover image, found the two ways EPUBs declare one: an EPUB2
  // <meta name="cover"> pointing at a manifest id, or an EPUB3 item with
  // "cover-image" in its properties. Streamed like any entry; open/read/close
  // must not overlap an open chapter.
  static constexpr int COVER_NONE = 0, COVER_JPEG = 1, COVER_PNG = 2;
  int coverType() const { return _coverType; }
  bool coverOpen();
  int coverRead(uint8_t* dst, int n) { return entryRead(dst, n); }
  void coverClose() { entryClose(); }
  uint32_t coverSize() const { return _cover.usize; }

 private:
  struct Ent {
    uint32_t hrefHash;  // idref hash, then resolved-path hash; see parseOpf
    uint32_t lho;       // local header offset
    uint32_t csize, usize;
    uint16_t method;    // 0 stored, 8 deflate
    uint8_t ok;         // 0 idref only, 1 path known, 2 zip entry found
  };

  template <typename F>
  bool walkCD(F cb);
  bool findEntry(const char* name, Ent& out);
  bool parseContainer(char* opfPath, int cap);
  bool parseOpf(const char* opfPath);
  template <typename F>
  void scanTags(F cb);

  bool entryOpen(const Ent& e);
  void entryClose();
  int entryRead(uint8_t* dst, int n);

  int tokNextByte();
  void tokEmitCp(uint32_t cp, const char* utf8, int len);
  void tokFlushWord();
  void tokBlockBreak();
  int tokPump();

  IO* _io = nullptr;
  Ent* _spine = nullptr;
  int _spineN = 0;
  uint32_t _cdOfs = 0;
  int _cdCount = 0;
  const char* _err = "";
  char _opfDir[128] = "";
  Ent _cover{};
  Ent _blob{};
  char _chapDir[128] = "";   // the open chapter's folder, for resolving <img>
  char _imgName[192] = "";   // the image a TOK_IMAGE is about
  bool _imgReady = false;
  uint32_t _coverPathHash = 0;
  uint8_t _coverType = 0;
  bool _coverOk = false;

  // entry stream state
  void* _inflator = nullptr;    // tinfl_decompressor, heap (~11 KB)
  uint8_t* _window = nullptr;   // 32 KB wrapping dictionary for streaming inflate
  uint8_t* _inBuf = nullptr;    // 2 KB compressed-input chunk
  int _inLen = 0, _inPos = 0;
  uint32_t _filePos = 0, _compLeft = 0, _rawLeft = 0;
  uint32_t _winPos = 0, _winAvail = 0, _winRead = 0;
  uint16_t _method = 0;
  bool _entryOpenF = false;

  // tokenizer state
  bool _chapterOpenF = false;
  bool _insideBody = false;
  bool _ended = false;
  int _nonVisibleDepth = 0;
  uint32_t _offset = 0;         // visible codepoints so far, CrossPoint counting
  uint8_t _tokBuf[512];
  int _tokLen = 0, _tokPos = 0;
  int _pushByte = -1;
  char _cpBuf[4];
  int _cpLen = 0;
  // word assembly, with a one-word output slot: whitespace, block boundaries
  // and over-long words all complete a word by moving it to the slot, and
  // next() drains the slot (then any queued PARA/END) before pumping more.
  char _word[WORD_CAP];
  int _wordLen = 0;
  uint32_t _wordStart = 0;
  char _outWord[WORD_CAP];
  uint32_t _outStart = 0;
  bool _outReady = false;
  bool _wordSinceBreak = false;
  int _queued = 0;              // TOK_PARA or TOK_END waiting behind the slot
};

// --- CrossPoint sidecar compatibility ---------------------------------------
// The progress file lives at <cacheDir(bookPath)>/progress.bin. The path hash
// is 32-bit libstdc++ std::hash<std::string> (MurmurHashUnaligned2, seed
// 0xc70f6907) of the book's absolute path on the card -- verified bit-exact
// against a real 32-bit libstdc++. Both firmwares run 32-bit toolchains, so
// both derive the same directory from the same path string.
uint32_t cpHash(const char* s, size_t len);
void cacheDir(const char* bookPath, char* out, int cap);   // "/.crosspoint/epub_<hash>"

struct Progress {
  uint16_t spine = 0;
  uint16_t page = 0;        // advisory: CrossPoint re-derives the page from offset
  uint16_t pageCount = 0;   // advisory
  uint32_t offset = 0;      // the portable position
  bool hasOffset = false;
};

int encodeProgress(const Progress& p, uint8_t out[10]);       // returns byte count
bool decodeProgress(const uint8_t* d, int n, Progress& out);  // accepts 4/6/10 bytes

}  // namespace epubc
