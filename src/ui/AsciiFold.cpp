#include "ui/AsciiFold.h"

#include <cstdint>

std::string ascii_fold_latin(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char first = (unsigned char)text[i];
        if (first < 0x80) {
            out.push_back((char)first);
            ++i;
            continue;
        }
        uint32_t cp = 0;
        size_t count = 0;
        if ((first & 0xe0) == 0xc0 && i + 1 < text.size()) {
            cp = ((uint32_t)(first & 0x1f) << 6) | ((uint32_t)text[i + 1] & 0x3f);
            count = 2;
        } else if ((first & 0xf0) == 0xe0 && i + 2 < text.size()) {
            cp = ((uint32_t)(first & 0x0f) << 12) |
                 (((uint32_t)text[i + 1] & 0x3f) << 6) | ((uint32_t)text[i + 2] & 0x3f);
            count = 3;
        } else {
            ++i;
            continue;
        }
        if (cp == 0xC4 || cp == 0xD6 || cp == 0xDC || cp == 0xE4 || cp == 0xF6 || cp == 0xFC || cp == 0xDF) {
            out.append(text, i, count);i += count;continue;
        }
        i += count;
        if ((cp >= 0x00c0 && cp <= 0x00c5) || cp == 0x0100 || cp == 0x0102 || cp == 0x0104) out += 'A';
        else if ((cp >= 0x00e0 && cp <= 0x00e5) || cp == 0x0101 || cp == 0x0103 || cp == 0x0105) out += 'a';
        else if (cp == 0x00c7 || cp == 0x0106 || cp == 0x010c) out += 'C';
        else if (cp == 0x00e7 || cp == 0x0107 || cp == 0x010d) out += 'c';
        else if ((cp >= 0x00c8 && cp <= 0x00cb) || cp == 0x0112 || cp == 0x0118) out += 'E';
        else if ((cp >= 0x00e8 && cp <= 0x00eb) || cp == 0x0113 || cp == 0x0119) out += 'e';
        else if (cp >= 0x00cc && cp <= 0x00cf) out += 'I';
        else if (cp >= 0x00ec && cp <= 0x00ef) out += 'i';
        else if (cp == 0x00d1 || cp == 0x0143) out += 'N';
        else if (cp == 0x00f1 || cp == 0x0144) out += 'n';
        else if ((cp >= 0x00d2 && cp <= 0x00d6) || cp == 0x00d8) out += 'O';
        else if ((cp >= 0x00f2 && cp <= 0x00f6) || cp == 0x00f8) out += 'o';
        else if (cp >= 0x00d9 && cp <= 0x00dc) out += 'U';
        else if (cp >= 0x00f9 && cp <= 0x00fc) out += 'u';
        else if (cp == 0x00dd || cp == 0x0178) out += 'Y';
        else if (cp == 0x00fd || cp == 0x00ff) out += 'y';
        else if (cp == 0x00df) out += "ss";
        else if (cp == 0x0141) out += 'L';
        else if (cp == 0x0142) out += 'l';
        else if (cp == 0x015a || cp == 0x0160) out += 'S';
        else if (cp == 0x015b || cp == 0x0161) out += 's';
        else if (cp == 0x0179 || cp == 0x017b || cp == 0x017d) out += 'Z';
        else if (cp == 0x017a || cp == 0x017c || cp == 0x017e) out += 'z';
        else if (cp == 0x2013 || cp == 0x2014) out += '-';
        else if (cp == 0x2018 || cp == 0x2019) out += '\'';
    }
    return out;
}
