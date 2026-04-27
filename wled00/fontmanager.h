#pragma once
/*
 * FontManager class for managing wbf format font loading, caching, and rendering
 * Supports up to 5 fonts (0-4) from flash or file system
 * Caches glyphs in segment data for fast access during rendering
 *
 * (c) 2026 by @dedehai
 */

#include <stddef.h>
#include <stdint.h>

class Segment;  // forward declaration

enum {
LAST_ASCII_CHAR = \
  127,  // last standard ASCII char, higher chars are mapped to unicode offset set in font header and accessed via
       // unicode values, not direct index, see getGlyphIndex()
FONT_HEADER_SIZE = 12
};
/**
 * Font format:
 *
 * Header Layout (12 Bytes):
 * [0]   Magic 'W' (0x57)
 * [1]   Glyph height
 * [2]   Fixed/max glyph width
 * [3]   Spacing between chars
 * [4]   Flags: (0x01 = variable width)
 * [5]   First Char
 * [6]   Last Char
 * [7]   reserved: 0x00
 * [8-11] Unicode Offset (32-bit little-endian)
 *
 * Followed by:
 * - Width table (if variable width): [first..last] byte array omitted for fixed width fonts i.e. glyphs start after
 * header
 * - Bitmap data: bit-packed glyphs - top left to bottom right, row by row, MSB first, see src/font files for example
 */

// Glyph entry in RAM cache
struct GlyphEntry {
  uint8_t code;      // Glyph index (0-255)
  uint8_t width;     // Width in pixels
  uint8_t height;    // Height in pixels
  uint8_t reserved;  // Padding to keep FontHeader 4-byte aligned
};

// Segment metadata (stored BEFORE the font data in segment data)
struct SegmentFontMetadata {
  uint8_t availableFonts;  // Bitflags for available fonts on FS: set to 1 << fontNum if font is available in FS (0-4)
  uint8_t cachedFontNum;   // Currently cached font (0-4, 0xFF = none, highest bit set = file font)
  uint8_t lastFontNum;     // font number requested in last call
  uint8_t glyphCount;      // Number of glyphs cached
};

// Memory layout of cached font in segment data:
// [SegmentFontMetadata] - 4 bytes
// [GlyphEntry array]
// [12-byte font header] - copy of the relevant font header data
// [Bitmap data] - sequential, matches registry order

static constexpr uint8_t kMaxCachedGlyphs =
    64;  // max segment string length is 64 chars so this is absolute worst case
static constexpr uint8_t kMaxFonts             = 5;   // scrolli text supports font numbers 0-4
static constexpr size_t  kFontNameBufferSize = 64;  // font names

// font header, identical to wbf header, size must be FONT_HEADER_SIZE
struct FontHeader {
  uint8_t magic;   // should be 'W' (0x57)
  uint8_t height;  // TODO: claudio - should we use the padding bytes and store a full copy of the header? might make copying the
                   // header easier?
  uint8_t  width;
  uint8_t  spacing;
  uint8_t  flags;
  uint8_t  first;
  uint8_t  last;
  uint8_t  reserved;  // should be 0x00
  uint32_t firstUnicode;
};
static_assert(sizeof(FontHeader) == FONT_HEADER_SIZE, "FontHeader size must be exactly FONT_HEADER_SIZE bytes");

class FontManager {
 public:
  explicit FontManager(Segment* seg)
      : segment_(seg) {
  }

  bool loadFont(uint8_t fontNum, const char* text, bool useFile);
  void cacheNumbers(bool cache) {
    _cacheNumbers = cache;
  }
  void cacheGlyphs(const char* text);

  // Get dimensions (use cached header)
  uint8_t getFontHeight() {
    return reinterpret_cast<FontHeader*>(_fontBase)->height;
  }
  uint8_t getFontWidth() {
    return reinterpret_cast<FontHeader*>(_fontBase)->width;
  }
  uint8_t getFontSpacing() {
    return reinterpret_cast<FontHeader*>(_fontBase)->spacing;
  }
  uint8_t getGlyphWidth(uint32_t unicode);

  // Rendering
  void drawCharacter(uint32_t unicode, int16_t x, int16_t y, uint32_t color, uint32_t col2, int8_t rotate);

 private:
  Segment* segment_;
  uint8_t  _fontNum{0};      // Font number (0-4)
  bool     _useFileFont{false};  // true = file, false = flash
  bool     _cacheNumbers{false};
  uint8_t* _fontBase{nullptr};  // pointer to start of font data (header + bitmaps) in segment data

  // get metadata pointer
  SegmentFontMetadata* getMetadata();

  void updateFontBase();

  uint8_t* getGlyphBitmap(uint32_t unicode, uint8_t& outWidth, uint8_t& outHeight);

  // Glyph index calculation (pure function, inline for speed)
  static int32_t getGlyphIndex(uint32_t unicode, FontHeader* hdr);

  // File font management
  void    getFontFileName(uint8_t fontNum, char* buffer, bool scanAll = false);
  void    scanAvailableFonts();
  void    rebuildCache(const char* text);
  uint8_t collectNeededCodes(const char* text, FontHeader* hdr, uint8_t* outCodes);
};
