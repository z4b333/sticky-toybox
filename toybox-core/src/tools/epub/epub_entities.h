// HTML named-entity table for the EPUB reader's XHTML tokenizer.
//
// Copied from CrossPoint Reader's htmlEntities.cpp (MIT, Copyright (c) 2025
// Dave Allie), itself based on atomic14's diy-esp32-epub-reader (MIT) -- see
// THIRD-PARTY.md. Copied VERBATIM rather than rewritten because reading
// positions are exchanged with CrossPoint as visible-codepoint offsets: both
// firmwares must expand (or fail to expand) exactly the same entities, or a
// bookmark drifts by the difference.
//
// Sorted lexicographically by key, for binary search. Keys include the & and ;.
#pragma once
#include <cstring>

namespace epubent {

struct EntityPair {
  const char* key;
  const char* value;
};

static constexpr EntityPair TABLE[] = {
    {"&AElig;", "Æ"},      {"&Aacute;", "Á"},      {"&Acirc;", "Â"},      {"&Agrave;", "À"},  {"&Alpha;", "Α"},
    {"&Aring;", "Å"},      {"&Atilde;", "Ã"},      {"&Auml;", "Ä"},       {"&Beta;", "Β"},    {"&Ccedil;", "Ç"},
    {"&Chi;", "Χ"},        {"&Dagger;", "‡"},      {"&Delta;", "Δ"},      {"&ETH;", "Ð"},     {"&Eacute;", "É"},
    {"&Ecirc;", "Ê"},      {"&Egrave;", "È"},      {"&Epsilon;", "Ε"},    {"&Eta;", "Η"},     {"&Euml;", "Ë"},
    {"&Gamma;", "Γ"},      {"&Iacute;", "Í"},      {"&Icirc;", "Î"},      {"&Igrave;", "Ì"},  {"&Iota;", "Ι"},
    {"&Iuml;", "Ï"},       {"&Kappa;", "Κ"},       {"&Lambda;", "Λ"},     {"&Mu;", "Μ"},      {"&Ntilde;", "Ñ"},
    {"&Nu;", "Ν"},         {"&OElig;", "Œ"},       {"&Oacute;", "Ó"},     {"&Ocirc;", "Ô"},   {"&Ograve;", "Ò"},
    {"&Omega;", "Ω"},      {"&Omicron;", "Ο"},     {"&Oslash;", "Ø"},     {"&Otilde;", "Õ"},  {"&Ouml;", "Ö"},
    {"&Phi;", "Φ"},        {"&Pi;", "Π"},          {"&Prime;", "″"},      {"&Psi;", "Ψ"},     {"&Rho;", "Ρ"},
    {"&Scaron;", "Š"},     {"&Sigma;", "Σ"},       {"&THORN;", "Þ"},      {"&Tau;", "Τ"},     {"&Theta;", "Θ"},
    {"&Uacute;", "Ú"},     {"&Ucirc;", "Û"},       {"&Ugrave;", "Ù"},     {"&Upsilon;", "Υ"}, {"&Uuml;", "Ü"},
    {"&Xi;", "Ξ"},         {"&Yacute;", "Ý"},      {"&Yuml;", "Ÿ"},       {"&Zeta;", "Ζ"},    {"&aacute;", "á"},
    {"&acirc;", "â"},      {"&acute;", "´"},       {"&aelig;", "æ"},      {"&agrave;", "à"},  {"&alefsym;", "ℵ"},
    {"&alpha;", "α"},      {"&amp;", "&"},         {"&and;", "∧"},        {"&ang;", "∠"},     {"&aring;", "å"},
    {"&asymp;", "≈"},      {"&atilde;", "ã"},      {"&auml;", "ä"},       {"&bdquo;", "„"},   {"&beta;", "β"},
    {"&brvbar;", "¦"},     {"&bull;", "•"},        {"&cap;", "∩"},        {"&ccedil;", "ç"},  {"&cedil;", "¸"},
    {"&cent;", "¢"},       {"&chi;", "χ"},         {"&circ;", "ˆ"},       {"&clubs;", "♣"},   {"&cong;", "≅"},
    {"&copy;", "©"},       {"&crarr;", "↵"},       {"&cup;", "∪"},        {"&curren;", "¤"},  {"&dArr;", "⇓"},
    {"&dagger;", "†"},     {"&darr;", "↓"},        {"&deg;", "°"},        {"&delta;", "δ"},   {"&diams;", "♦"},
    {"&divide;", "÷"},     {"&eacute;", "é"},      {"&ecirc;", "ê"},      {"&egrave;", "è"},  {"&empty;", "∅"},
    {"&emsp;", " "},       {"&ensp;", " "},        {"&epsilon;", "ε"},    {"&equiv;", "≡"},   {"&eta;", "η"},
    {"&eth;", "ð"},        {"&euml;", "ë"},        {"&euro;", "€"},       {"&exist;", "∃"},   {"&fnof;", "ƒ"},
    {"&forall;", "∀"},     {"&frac12;", "½"},      {"&frac14;", "¼"},     {"&frac34;", "¾"},  {"&frasl;", "⁄"},
    {"&gamma;", "γ"},      {"&ge;", "≥"},          {"&gt;", ">"},         {"&hArr;", "⇔"},    {"&harr;", "↔"},
    {"&hearts;", "♥"},     {"&hellip;", "…"},      {"&iacute;", "í"},     {"&icirc;", "î"},   {"&iexcl;", "¡"},
    {"&igrave;", "ì"},     {"&image;", "ℑ"},       {"&infin;", "∞"},      {"&int;", "∫"},     {"&iota;", "ι"},
    {"&iquest;", "¿"},     {"&isin;", "∈"},        {"&iuml;", "ï"},       {"&kappa;", "κ"},   {"&lArr;", "⇐"},
    {"&lambda;", "λ"},     {"&lang;", "〈"},       {"&laquo;", "«"},      {"&larr;", "←"},    {"&lceil;", "⌈"},
    {"&ldquo;", "\u201C"}, {"&le;", "≤"},          {"&lfloor;", "⌊"},     {"&lowast;", "∗"},  {"&loz;", "◊"},
    {"&lrm;", "\u200E"},   {"&lsaquo;", "‹"},      {"&lsquo;", "\u2018"}, {"&lt;", "<"},      {"&macr;", "¯"},
    {"&mdash;", "—"},      {"&micro;", "µ"},       {"&middot;", "·"},     {"&minus;", "−"},   {"&mu;", "μ"},
    {"&nabla;", "∇"},      {"&nbsp;", "\xC2\xA0"}, {"&ndash;", "–"},      {"&ne;", "≠"},      {"&ni;", "∋"},
    {"&not;", "¬"},        {"&notin;", "∉"},       {"&nsub;", "⊄"},       {"&ntilde;", "ñ"},  {"&nu;", "ν"},
    {"&oacute;", "ó"},     {"&ocirc;", "ô"},       {"&oelig;", "œ"},      {"&ograve;", "ò"},  {"&oline;", "‾"},
    {"&omega;", "ω"},      {"&omicron;", "ο"},     {"&oplus;", "⊕"},      {"&or;", "∨"},      {"&ordf;", "ª"},
    {"&ordm;", "º"},       {"&oslash;", "ø"},      {"&otilde;", "õ"},     {"&otimes;", "⊗"},  {"&ouml;", "ö"},
    {"&para;", "¶"},       {"&part;", "∂"},        {"&permil;", "‰"},     {"&perp;", "⊥"},    {"&phi;", "φ"},
    {"&pi;", "π"},         {"&piv;", "ϖ"},         {"&plusmn;", "±"},     {"&pound;", "£"},   {"&prime;", "′"},
    {"&prod;", "∏"},       {"&prop;", "∝"},        {"&psi;", "ψ"},        {"&quot;", "\""},   {"&rArr;", "⇒"},
    {"&radic;", "√"},      {"&rang;", "〉"},       {"&raquo;", "»"},      {"&rarr;", "→"},    {"&rceil;", "⌉"},
    {"&rdquo;", "\u201D"}, {"&real;", "\u211C"},   {"&reg;", "®"},        {"&rfloor;", "⌋"},  {"&rho;", "ρ"},
    {"&rlm;", "\u200F"},   {"&rsaquo;", "›"},      {"&rsquo;", "\u2019"}, {"&sbquo;", "‚"},   {"&scaron;", "š"},
    {"&sdot;", "⋅"},       {"&sect;", "§"},        {"&shy;", "\xC2\xAD"}, {"&sigma;", "σ"},   {"&sigmaf;", "ς"},
    {"&sim;", "∼"},        {"&spades;", "♠"},      {"&sub;", "⊂"},        {"&sube;", "⊆"},    {"&sum;", "∑"},
    {"&sup1;", "¹"},       {"&sup2;", "²"},        {"&sup3;", "³"},       {"&sup;", "⊃"},     {"&supe;", "⊇"},
    {"&szlig;", "ß"},      {"&tau;", "τ"},         {"&there4;", "∴"},     {"&theta;", "θ"},   {"&thetasym;", "ϑ"},
    {"&thinsp;", " "},     {"&thorn;", "þ"},       {"&tilde;", "˜"},      {"&times;", "×"},   {"&trade;", "™"},
    {"&uArr;", "⇑"},       {"&uacute;", "ú"},      {"&uarr;", "↑"},       {"&ucirc;", "û"},   {"&ugrave;", "ù"},
    {"&uml;", "¨"},        {"&upsih;", "ϒ"},       {"&upsilon;", "υ"},    {"&uuml;", "ü"},    {"&weierp;", "℘"},
    {"&xi;", "ξ"},         {"&yacute;", "ý"},      {"&yen;", "¥"},        {"&yuml;", "ÿ"},    {"&zeta;", "ζ"},
    {"&zwj;", "\u200D"},   {"&zwnj;", "\u200C"},
};

inline const char* lookup(const char* key, size_t len) {
  int lo = 0, hi = (int)(sizeof(TABLE) / sizeof(TABLE[0])) - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const char* k = TABLE[mid].key;
    const size_t kl = strlen(k);
    const size_t n = len < kl ? len : kl;
    int c = memcmp(key, k, n);
    if (c == 0) c = (int)len - (int)kl;
    if (c == 0) return TABLE[mid].value;
    if (c < 0) hi = mid - 1; else lo = mid + 1;
  }
  return nullptr;
}

}  // namespace epubent
