#include "scoreboard.h"

struct Rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

static constexpr Rgb COLOR_BLACK = {0, 0, 0};
static constexpr Rgb COLOR_WHITE = {220, 235, 255};
static constexpr Rgb COLOR_YELLOW = {255, 190, 35};
static constexpr Rgb COLOR_CYAN = {40, 210, 255};
static constexpr Rgb COLOR_RED = {255, 0, 0};
static constexpr Rgb COLOR_RUNNER_AWAY = {35, 140, 255};
static constexpr Rgb COLOR_RUNNER_HOME = {255, 0, 0};

static uint8_t pixelBuffer[SCOREBOARD_RGB_BYTES];
static uint8_t pngRawBuffer[SCOREBOARD_HEIGHT * (1 + SCOREBOARD_WIDTH * 3)];

static const char *const SCOREBOARD_TEMPLATE[SCOREBOARD_HEIGHT] = {
  ".....CC...C..C...C....CC......................WWWWWWWWWWWWWWWWWWWwWWWWWwWWWWWwWWWWWW#WWWWWWWWWW#",
  ".....C.C.C.C.C...C...C...............##.......WW.................w.....w.....w.....W#W........W#",
  ".....CC..CCC.C...C....C..............##.......WW.c.c.c..cc.c.ccc.w.....w.....w.....W#W........W#",
  ".....C.C.C.C.C...C.....C............G..G......WW.c.c.c.c...c..c..w.....w.....w.....W#W........W#",
  ".....CC..C.C.CCC.CCC.CC............G....G.....WW.c.c.c..c..c..c..w.....w.....w.....W#W........W#",
  "......YY.YYY.YY..Y.Y.Y.YYY..YY....G......G....WW..c..c...c.c..c..w.....w.....w.....W#W........W#",
  ".....Y....Y..Y.Y.Y.Y.Y.Y...Y.....G........G...WW..c..c.cc..c..c..w.....w.....w.....W#W........W#",
  "......Y...Y..YY..Y.YY..YY...Y..W#..........##.Wwwwwwwwwwwwwwwwwwwpwwwwwpwwwwwpwwwwww#wwwwwwwwww#",
  ".......Y..Y..Y.Y.Y.Y.Y.Y.....Y.W#....##....##.Wwwwwwwwwwwwwwwwwwwpwwwwwpwwwwwpwwwwww#wwwwwwwwww#",
  ".....YY...Y..Y.Y.Y.Y.Y.YYY.YY....G........G...WW.................w.....w.....w.....W#W........W#",
  "......C..C.C.CCC..CC..............G......G....WW.Y.Y..Y..Y.Y.YYY.w.....w.....w.....W#W........W#",
  ".....C.C.C.C..C..C.................G....G.....WW.Y.Y.Y.Y.YYY.Y...w.....w.....w.....W#W........W#",
  ".....C.C.C.C..C...C.................G..G......WW.YYY.Y.Y.YYY.YY..w.....w.....w.....W#W........W#",
  ".....C.C.C.C..C....C.................##.......WW.Y.Y.Y.Y.Y.Y.Y...w.....w.....w.....W#W........W#",
  "......C...C...C..CC..................##.......WW.Y.Y..Y..Y.Y.YYY.w.....w.....w.....W#W........W#",
  "..............................................WWWWWWWWWWWWWWWWWWWwWWWWWwWWWWWwWWWWWW#WWWWWWWWWW#"
};

static const uint8_t GLYPH_SPACE[5] = {
  0b000,
  0b000,
  0b000,
  0b000,
  0b000
};

static const uint8_t *glyphFor(char c) {
  static const uint8_t digits[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},
    {0b010, 0b110, 0b010, 0b010, 0b111},
    {0b111, 0b001, 0b111, 0b100, 0b111},
    {0b111, 0b001, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b111, 0b001, 0b001},
    {0b111, 0b100, 0b111, 0b001, 0b111},
    {0b111, 0b100, 0b111, 0b101, 0b111},
    {0b111, 0b001, 0b010, 0b010, 0b010},
    {0b111, 0b101, 0b111, 0b101, 0b111},
    {0b111, 0b101, 0b111, 0b001, 0b111}
  };

  static const uint8_t glyphA[5] = {0b010, 0b101, 0b111, 0b101, 0b101};
  static const uint8_t glyphC[5] = {0b111, 0b100, 0b100, 0b100, 0b111};
  static const uint8_t glyphE[5] = {0b111, 0b100, 0b111, 0b100, 0b111};
  static const uint8_t glyphG[5] = {0b111, 0b100, 0b101, 0b101, 0b111};
  static const uint8_t glyphH[5] = {0b101, 0b101, 0b111, 0b101, 0b101};
  static const uint8_t glyphI[5] = {0b111, 0b010, 0b010, 0b010, 0b111};
  static const uint8_t glyphM[5] = {0b101, 0b111, 0b101, 0b101, 0b101};
  static const uint8_t glyphN[5] = {0b101, 0b111, 0b111, 0b111, 0b101};
  static const uint8_t glyphO[5] = {0b111, 0b101, 0b101, 0b101, 0b111};
  static const uint8_t glyphR[5] = {0b111, 0b101, 0b111, 0b110, 0b101};
  static const uint8_t glyphS[5] = {0b111, 0b100, 0b111, 0b001, 0b111};
  static const uint8_t glyphT[5] = {0b111, 0b010, 0b010, 0b010, 0b010};
  static const uint8_t glyphW[5] = {0b101, 0b101, 0b101, 0b101, 0b111};
  static const uint8_t glyphY[5] = {0b101, 0b101, 0b111, 0b010, 0b010};
  static const uint8_t glyphBang[5] = {0b010, 0b010, 0b010, 0b000, 0b010};
  static const uint8_t glyphB[5] = {0b110, 0b101, 0b110, 0b101, 0b110};
  static const uint8_t glyphDash[5] = {0b000, 0b000, 0b111, 0b000, 0b000};

  if (c >= '0' && c <= '9') return digits[c - '0'];
  if (c >= 'a' && c <= 'z') {
    c = (char)(c - 'a' + 'A');
  }
  if (c == 'A') return glyphA;
  if (c == 'B') return glyphB;
  if (c == 'C') return glyphC;
  if (c == 'E') return glyphE;
  if (c == 'G') return glyphG;
  if (c == 'H') return glyphH;
  if (c == 'I') return glyphI;
  if (c == 'M') return glyphM;
  if (c == 'N') return glyphN;
  if (c == 'O') return glyphO;
  if (c == 'R') return glyphR;
  if (c == 'S') return glyphS;
  if (c == 'T') return glyphT;
  if (c == 'W') return glyphW;
  if (c == 'Y') return glyphY;
  if (c == '!') return glyphBang;
  if (c == '-') return glyphDash;
  return GLYPH_SPACE;
}

static void putPixel(uint8_t x, uint8_t y, Rgb color) {
  if (x >= SCOREBOARD_WIDTH || y >= SCOREBOARD_HEIGHT) {
    return;
  }

  const size_t offset = ((size_t)y * SCOREBOARD_WIDTH + x) * 3;
  pixelBuffer[offset] = color.r;
  pixelBuffer[offset + 1] = color.g;
  pixelBuffer[offset + 2] = color.b;
}

static void drawChar(uint8_t x, uint8_t y, char c, Rgb color) {
  const uint8_t *glyph = glyphFor(c);
  for (uint8_t row = 0; row < 5; row++) {
    for (uint8_t col = 0; col < 3; col++) {
      if ((glyph[row] & (0b100 >> col)) != 0) {
        putPixel(x + col, y + row, color);
      }
    }
  }
}

static uint8_t textWidth(const char *text) {
  uint8_t width = 0;
  for (const char *cursor = text; *cursor != '\0'; cursor++) {
    if (*cursor == ' ') {
      width += 2;
    } else {
      width += 4;
    }
  }
  return width;
}

static void drawText(uint8_t x, uint8_t y, const char *text, Rgb color) {
  uint8_t cursorX = x;
  for (const char *cursor = text; *cursor != '\0'; cursor++) {
    if (*cursor == ' ') {
      cursorX += 2;
      continue;
    }
    drawChar(cursorX, y, *cursor, color);
    cursorX += 4;
  }
}

static Rgb templateColor(char c) {
  switch (c) {
    case 'C': return {38, 188, 200};
    case 'c': return {24, 136, 145};
    case 'W': return {251, 251, 251};
    case 'w': return {247, 247, 247};
    case '#': return {255, 255, 255};
    case 'p': return {239, 239, 239};
    case 'Y': return {225, 246, 47};
    case 'G': return {0, 255, 18};
    default: return COLOR_BLACK;
  }
}

static void drawTemplate() {
  for (uint8_t y = 0; y < SCOREBOARD_HEIGHT; y++) {
    for (uint8_t x = 0; x < SCOREBOARD_WIDTH; x++) {
      putPixel(x, y, templateColor(SCOREBOARD_TEMPLATE[y][x]));
    }
  }
}

static void fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, Rgb color) {
  for (uint8_t yy = 0; yy < h; yy++) {
    for (uint8_t xx = 0; xx < w; xx++) {
      putPixel(x + xx, y + yy, color);
    }
  }
}

static void drawBaseBox(uint8_t x, uint8_t y, bool occupied, Rgb teamColor) {
  fillRect(x, y, 2, 2, occupied ? teamColor : COLOR_WHITE);
}

static Rgb runnerTeamColor(bool topHalf) {
  return topHalf ? COLOR_RUNNER_AWAY : COLOR_RUNNER_HOME;
}

static void drawSingleDigit(uint8_t x, uint8_t y, uint8_t value, Rgb color) {
  drawChar(x, y, '0' + min((uint8_t)9, value), color);
}

static void drawTotal(uint8_t x, uint8_t y, uint8_t value, Rgb color) {
  value = min((uint8_t)99, value);
  if (value < 10) {
    drawSingleDigit(x + 2, y, value, color);
    return;
  }

  drawSingleDigit(x, y, value / 10, color);
  drawSingleDigit(x + 4, y, value % 10, color);
}

static void drawScoreRow(
  uint8_t y,
  const uint8_t inningRuns[3],
  uint8_t total,
  Rgb color
) {
  fillRect(66, y, 5, 5, COLOR_BLACK);
  fillRect(72, y, 5, 5, COLOR_BLACK);
  fillRect(78, y, 5, 5, COLOR_BLACK);
  fillRect(86, y, 8, 5, COLOR_BLACK);

  drawSingleDigit(67, y, inningRuns[0], color);
  drawSingleDigit(73, y, inningRuns[1], color);
  drawSingleDigit(79, y, inningRuns[2], color);
  drawTotal(87, y, total, color);
}

static void renderFramebuffer(const ScoreboardState &state) {
  drawTemplate();

  // Count colors contrast with their labels (cyan labels -> yellow digits, etc.).
  drawSingleDigit(0, 0, state.balls, COLOR_YELLOW);
  drawSingleDigit(0, 5, state.strikes, COLOR_CYAN);
  drawSingleDigit(0, 10, state.outs, COLOR_YELLOW);

  const Rgb runnerColor = runnerTeamColor(state.topHalf);
  drawBaseBox(37, 1, state.runnerSecond, runnerColor);
  drawBaseBox(43, 7, state.runnerFirst, runnerColor);
  drawBaseBox(31, 7, state.runnerThird, runnerColor);

  drawScoreRow(2, state.awayInningRuns, state.awayScore, COLOR_CYAN);
  drawScoreRow(10, state.homeInningRuns, state.homeScore, COLOR_YELLOW);
}

static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t length) {
  crc = ~crc;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
    }
  }
  return ~crc;
}

static uint32_t adler32(const uint8_t *data, size_t length) {
  uint32_t a = 1;
  uint32_t b = 0;

  for (size_t i = 0; i < length; i++) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }

  return (b << 16) | a;
}

static bool writeByte(uint8_t *out, size_t capacity, size_t &pos, uint8_t value) {
  if (pos >= capacity) {
    return false;
  }

  out[pos++] = value;
  return true;
}

static bool writeBytes(
  uint8_t *out,
  size_t capacity,
  size_t &pos,
  const uint8_t *data,
  size_t length
) {
  if (pos + length > capacity) {
    return false;
  }

  if (length > 0) {
    memcpy(out + pos, data, length);
  }
  pos += length;
  return true;
}

static bool writeBE32(uint8_t *out, size_t capacity, size_t &pos, uint32_t value) {
  return writeByte(out, capacity, pos, (value >> 24) & 0xFF)
    && writeByte(out, capacity, pos, (value >> 16) & 0xFF)
    && writeByte(out, capacity, pos, (value >> 8) & 0xFF)
    && writeByte(out, capacity, pos, value & 0xFF);
}

static bool writeChunk(
  uint8_t *out,
  size_t capacity,
  size_t &pos,
  const char type[4],
  const uint8_t *data,
  size_t length
) {
  if (!writeBE32(out, capacity, pos, length)) {
    return false;
  }

  const size_t crcStart = pos;
  if (!writeBytes(out, capacity, pos, (const uint8_t *)type, 4)) {
    return false;
  }
  if (!writeBytes(out, capacity, pos, data, length)) {
    return false;
  }

  const uint32_t crc = crc32Update(0, out + crcStart, 4 + length);
  return writeBE32(out, capacity, pos, crc);
}

// Self-contained fixed-Huffman DEFLATE (RFC 1951) wrapped in a zlib stream
// (RFC 1950). The ESP32-C3 ROM miniz needs a heap allocation we cannot satisfy,
// so we encode here. The panel's PNG decoder requires real compressed data; a
// stored/uncompressed block is rejected.
static constexpr int LZ_HASH_BITS = 13;
static constexpr size_t LZ_HASH_SIZE = (size_t)1 << LZ_HASH_BITS;
static constexpr size_t LZ_MAX_RAW = SCOREBOARD_HEIGHT * (1 + SCOREBOARD_WIDTH * 3);

static uint16_t lzHead[LZ_HASH_SIZE];
static uint16_t lzPrev[LZ_MAX_RAW];

static const uint16_t LEN_BASE[29] = {
  3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
  67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t LEN_EXTRA[29] = {
  0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
  4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t DIST_BASE[30] = {
  1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
  769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t DIST_EXTRA[30] = {
  0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
  9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

struct BitWriter {
  uint8_t *out;
  size_t cap;
  size_t pos;
  uint32_t buf;
  int cnt;
  bool ok;

  void putBits(uint32_t value, int n) {
    if (n == 0) {
      return;
    }
    buf |= (value & (((uint32_t)1 << n) - 1)) << cnt;
    cnt += n;
    while (cnt >= 8) {
      if (pos >= cap) { ok = false; return; }
      out[pos++] = buf & 0xFF;
      buf >>= 8;
      cnt -= 8;
    }
  }

  // Huffman codes are packed most-significant-bit first.
  void putHuff(uint32_t code, int n) {
    for (int i = n - 1; i >= 0; i--) {
      putBits((code >> i) & 1, 1);
    }
  }

  void alignByte() {
    if (cnt > 0) {
      if (pos >= cap) { ok = false; return; }
      out[pos++] = buf & 0xFF;
      buf = 0;
      cnt = 0;
    }
  }

  void putByte(uint8_t b) {
    if (pos >= cap) { ok = false; return; }
    out[pos++] = b;
  }
};

static void putFixedLiteral(BitWriter &bw, uint16_t sym) {
  if (sym <= 143) {
    bw.putHuff(0x30 + sym, 8);
  } else if (sym <= 255) {
    bw.putHuff(0x190 + (sym - 144), 9);
  } else if (sym <= 279) {
    bw.putHuff(sym - 256, 7);
  } else {
    bw.putHuff(0xC0 + (sym - 280), 8);
  }
}

static void putFixedMatch(BitWriter &bw, int length, int distance) {
  int li = 28;
  while (li > 0 && LEN_BASE[li] > length) li--;
  putFixedLiteral(bw, 257 + li);
  bw.putBits(length - LEN_BASE[li], LEN_EXTRA[li]);

  int di = 29;
  while (di > 0 && DIST_BASE[di] > distance) di--;
  bw.putHuff(di, 5);
  bw.putBits(distance - DIST_BASE[di], DIST_EXTRA[di]);
}

static inline uint32_t lzHash(const uint8_t *d, size_t i) {
  uint32_t v = ((uint32_t)d[i] << 16) | ((uint32_t)d[i + 1] << 8) | d[i + 2];
  return (v * 2654435761u) >> (32 - LZ_HASH_BITS);
}

static size_t buildZlibCompressedBlock(
  uint8_t *out,
  size_t capacity,
  const uint8_t *data,
  size_t length
) {
  if (length > LZ_MAX_RAW || capacity < 6) {
    return 0;
  }

  memset(lzHead, 0, sizeof(lzHead));

  BitWriter bw{out, capacity, 0, 0, 0, true};
  bw.putByte(0x78); // zlib CMF: deflate, 32K window
  bw.putByte(0x01); // zlib FLG (check bits make header % 31 == 0)

  bw.putBits(1, 1); // BFINAL = 1
  bw.putBits(1, 2); // BTYPE = 01 (fixed Huffman)

  size_t i = 0;
  while (i < length && bw.ok) {
    int bestLen = 0;
    size_t bestPos = 0;

    if (i + 3 <= length) {
      const uint32_t h = lzHash(data, i);
      uint16_t cand = lzHead[h];
      int chain = 64;
      const size_t maxLen = min((size_t)258, length - i);
      while (cand != 0 && chain-- > 0) {
        const size_t c = cand - 1;
        if (i - c > 32768) break;
        size_t l = 0;
        while (l < maxLen && data[c + l] == data[i + l]) l++;
        if ((int)l > bestLen) {
          bestLen = (int)l;
          bestPos = c;
          if (l >= maxLen) break;
        }
        cand = lzPrev[c];
      }
      lzPrev[i] = lzHead[h];
      lzHead[h] = (uint16_t)(i + 1);
    }

    if (bestLen >= 3) {
      putFixedMatch(bw, bestLen, (int)(i - bestPos));
      for (int k = 1; k < bestLen; k++) {
        const size_t p = i + k;
        if (p + 3 <= length) {
          const uint32_t h = lzHash(data, p);
          lzPrev[p] = lzHead[h];
          lzHead[h] = (uint16_t)(p + 1);
        }
      }
      i += bestLen;
    } else {
      putFixedLiteral(bw, data[i]);
      i++;
    }
  }

  putFixedLiteral(bw, 256); // end of block
  bw.alignByte();

  const uint32_t adler = adler32(data, length);
  bw.putByte((adler >> 24) & 0xFF);
  bw.putByte((adler >> 16) & 0xFF);
  bw.putByte((adler >> 8) & 0xFF);
  bw.putByte(adler & 0xFF);

  return bw.ok ? bw.pos : 0;
}

static size_t buildZlibStoredBlock(
  uint8_t *out,
  size_t capacity,
  const uint8_t *data,
  size_t length
) {
  if (length > 65535 || capacity < length + 11) {
    return 0;
  }

  size_t pos = 0;
  out[pos++] = 0x78;
  out[pos++] = 0x01;
  out[pos++] = 0x01;
  out[pos++] = length & 0xFF;
  out[pos++] = (length >> 8) & 0xFF;
  const uint16_t nlen = ~((uint16_t)length);
  out[pos++] = nlen & 0xFF;
  out[pos++] = (nlen >> 8) & 0xFF;
  memcpy(out + pos, data, length);
  pos += length;

  const uint32_t adler = adler32(data, length);
  out[pos++] = (adler >> 24) & 0xFF;
  out[pos++] = (adler >> 16) & 0xFF;
  out[pos++] = (adler >> 8) & 0xFF;
  out[pos++] = adler & 0xFF;
  return pos;
}

static size_t encodePixelBufferPng(uint8_t *out, size_t outCapacity) {
  size_t rawPos = 0;
  for (uint8_t y = 0; y < SCOREBOARD_HEIGHT; y++) {
    pngRawBuffer[rawPos++] = 0x00;
    memcpy(
      pngRawBuffer + rawPos,
      pixelBuffer + ((size_t)y * SCOREBOARD_WIDTH * 3),
      SCOREBOARD_WIDTH * 3
    );
    rawPos += SCOREBOARD_WIDTH * 3;
  }

  static uint8_t idatBuffer[SCOREBOARD_PNG_MAX_BYTES];
  size_t idatLength =
    buildZlibCompressedBlock(idatBuffer, sizeof(idatBuffer), pngRawBuffer, rawPos);
  if (idatLength == 0) {
    idatLength =
      buildZlibStoredBlock(idatBuffer, sizeof(idatBuffer), pngRawBuffer, rawPos);
  }
  if (idatLength == 0) {
    return 0;
  }

  size_t pos = 0;
  static const uint8_t signature[] = {
    0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
  };
  if (!writeBytes(out, outCapacity, pos, signature, sizeof(signature))) {
    return 0;
  }

  uint8_t ihdr[13] = {
    0, 0, 0, SCOREBOARD_WIDTH,
    0, 0, 0, SCOREBOARD_HEIGHT,
    8, 2, 0, 0, 0
  };

  if (!writeChunk(out, outCapacity, pos, "IHDR", ihdr, sizeof(ihdr))) {
    return 0;
  }
  if (!writeChunk(out, outCapacity, pos, "IDAT", idatBuffer, idatLength)) {
    return 0;
  }
  if (!writeChunk(out, outCapacity, pos, "IEND", nullptr, 0)) {
    return 0;
  }

  return pos;
}

size_t renderBannerPng(
  const char *text,
  uint8_t *out,
  size_t outCapacity
) {
  if (out == nullptr || outCapacity == 0 || text == nullptr) {
    return 0;
  }

  memset(pixelBuffer, 0, sizeof(pixelBuffer));

  const uint8_t width = textWidth(text);
  const uint8_t startX =
    width >= SCOREBOARD_WIDTH ? 0 : (uint8_t)((SCOREBOARD_WIDTH - width) / 2);
  drawText(startX, 5, text, COLOR_WHITE);

  return encodePixelBufferPng(out, outCapacity);
}

size_t renderScoreboardPng(
  const ScoreboardState &state,
  uint8_t *out,
  size_t outCapacity
) {
  if (out == nullptr || outCapacity == 0) {
    return 0;
  }

  renderFramebuffer(state);

  const size_t pngLength = encodePixelBufferPng(out, outCapacity);
  if (pngLength == 0) {
    return 0;
  }

  Serial.print("Scoreboard PNG bytes=");
  Serial.println(pngLength);
  return pngLength;
}
