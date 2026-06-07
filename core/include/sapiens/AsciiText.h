#pragma once
// The simulators' baked font is printable-ASCII only (32..126). Transcripts can
// contain accents, smart quotes, dashes, etc. (UTF-8). to_ascii() transliterates
// the common cases (Latin-1 accents -> base letter, smart punctuation -> ASCII)
// and drops anything else, so the e-ink renderer never shows missing glyphs.

#include <string>

namespace sapiens::text {

inline const char* latin1_to_ascii(unsigned cp) {
    switch (cp) {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return "A";
        case 0xC6: return "AE";
        case 0xC7: return "C";
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: return "E";
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: return "I";
        case 0xD0: return "D";
        case 0xD1: return "N";
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD8: return "O";
        case 0xD9: case 0xDA: case 0xDB: case 0xDC: return "U";
        case 0xDD: return "Y";
        case 0xDF: return "ss";
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return "a";
        case 0xE6: return "ae";
        case 0xE7: return "c";
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: return "e";
        case 0xEC: case 0xED: case 0xEE: case 0xEF: return "i";
        case 0xF0: return "d";
        case 0xF1: return "n";
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: return "o";
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: return "u";
        case 0xFD: case 0xFF: return "y";
        default: return ""; // drop the rest
    }
}

inline std::string to_ascii(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    std::size_t i = 0, n = in.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) {
            if (c == '\n') out += '\n';
            else if (c == '\t') out += ' ';
            else if (c >= 32 && c <= 126) out += static_cast<char>(c);
            i += 1; // drop other control bytes
        } else if (c == 0xC2 && i + 1 < n) {
            if (static_cast<unsigned char>(in[i + 1]) == 0xA0) out += ' '; // nbsp
            i += 2; // drop ©, °, etc.
        } else if (c == 0xC3 && i + 1 < n) {
            unsigned char d = static_cast<unsigned char>(in[i + 1]);
            out += latin1_to_ascii(0xC0 + (d - 0x80)); // U+00C0..U+00FF
            i += 2;
        } else if (c == 0xE2 && i + 2 < n &&
                   static_cast<unsigned char>(in[i + 1]) == 0x80) {
            switch (static_cast<unsigned char>(in[i + 2])) {
                case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: out += '-'; break;
                case 0x98: case 0x99: case 0x9B: out += '\''; break;
                case 0x9C: case 0x9D: case 0x9E: out += '"'; break;
                case 0xA6: out += "..."; break; // ellipsis
                default: break;                 // drop
            }
            i += 3;
        } else {
            // unknown lead byte: skip it and any UTF-8 continuation bytes
            i += 1;
            while (i < n && (static_cast<unsigned char>(in[i]) & 0xC0) == 0x80) i += 1;
        }
    }
    return out;
}

} // namespace sapiens::text
