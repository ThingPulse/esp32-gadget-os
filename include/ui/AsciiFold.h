#pragma once

#include <string>

// Convert common Latin UTF-8 characters to glyphs available in the compact
// embedded Montserrat fonts, preserving the German fallback glyphs.
// Other unsupported non-ASCII code points are omitted.
std::string ascii_fold_latin(const std::string &text);
